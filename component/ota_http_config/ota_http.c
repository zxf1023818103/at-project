/*
 * ota_http.c - `ota_http <url>` console command: download an OTA_All.bin over HTTP or HTTPS and
 * apply it (write the inactive slot, toggle, reboot).
 *
 * HTTP layer = coreHTTP's request builder (HTTPClient_InitializeRequestHeaders) + coreHTTP's llhttp
 * parser driven in STREAMING mode: a single GET, and the response body is fed to llhttp whose
 * on_body callback streams each chunk straight to flash (ota_flash). This mirrors the efficient
 * single-GET streaming of the SDK's rtl8721d_ota.c (download_new_fw_from_server_http) - no HTTP
 * Range requests, no buffering of the ~820 KB image. URL parsing = the vendored urlparse.
 *
 * Transport: plain FreeRTOS+TCP socket for http://, the shared TLS_FreeRTOS_* transport for https://.
 */
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#ifdef AT_PROJECT_CARES
#include "cares_resolve.h"
#endif
#include "FreeRTOS_DNS.h"

#include <string.h>
#include <stdlib.h>

#include "urlparse.h"
#include "llhttp.h"
#include "core_http_client.h"
#include "at_log.h"                 /* rxi log.h enum must precede transport_tls.h's logging_levels.h LOG_* macros (mbedtls backend) */
#include "transport_tls.h"          /* TLS_FreeRTOS_send/recv/Disconnect, NetworkContext_t */
#include "transport_https.h"        /* shared Https_Connect (server-only trust, unified with AWS OTA) */
#include "ota_flash.h"
#include "FreeRTOS_CLI.h"
#include "psa/crypto.h"             /* psa_crypto_init - PSA backs picotls/mbedTLS RNG + the handshake */

/* The TLS transport leaves struct NetworkContext incomplete; each consumer defines it (the AWS demo
 * does the same). It holds just the TlsTransportParams_t the transport functions use. */
struct NetworkContext
{
    TlsTransportParams_t * pParams;
};

#define OTA_HTTP_RECV_TMO_MS    15000u
#define OTA_HTTP_SEND_TMO_MS    10000u
#define OTA_RECV_BUF            2048u
/* An AWS IoT OTA job's presigned S3 URL is signed with the OTA service role's STS TEMPORARY creds, so
 * it carries a long X-Amz-Security-Token (~600 chars) on top of the usual SigV4 query -> the path+query
 * runs ~1200 chars, well past a plain `aws s3 presign` (~380). Size the path + request buffers for it. */
#define OTA_REQ_BUF             2048u           /* GET request line (incl. full path) + headers */
#define OTA_PATH_BUF            2048u           /* path + ?query (AWS IoT STS-signed presigned URL) */
/* Connect (DNS + TLS) attempts: the FIRST resolve of a fresh host (e.g. the S3 bucket, never hit
 * before) can time out / "fail (no cache)" - the AWS MQTT path shows the same first-fail-then-ok. */
#define OTA_CONNECT_RETRIES     4u

/* Amazon Root CA 1 - the trust anchor for *.s3*.amazonaws.com (and most AWS endpoints). */
/* Server trust = the `ca/` folder on LittleFS (+ the embedded Mozilla CA bundle when
 * CRT_BUNDLE=ON); no root CA is baked into the source -- see creds.pRootCaPath below. */

/* ---- transport (http: plain TCP, https: TLS) ------------------------------------------------- */
typedef struct
{
    int                is_https;
    Socket_t           sock;                 /* http  */
    NetworkContext_t * tls;                  /* https */
    TlsTransportParams_t tlsParams;
} conn_t;

static int conn_connect( conn_t * c, int is_https, const char * host, uint16_t port )
{
    c->is_https = is_https;

    if( is_https )
    {
        static NetworkContext_t netctx;          /* TLS context (static: one OTA at a time) */
        TlsTransportStatus_t st;

        memset( c, 0, sizeof( *c ) );
        c->is_https = 1;
        netctx.pParams     = &c->tlsParams;
        c->tls             = &netctx;
        /* Shared server-only HTTPS connect (transport_https.c) — same config as the AWS OTA data
         * plane, so both verify S3 the same way (crt bundle / Amazon Root CA, picotls TLS 1.3). */
        st = Https_Connect( c->tls, host, port, OTA_HTTP_RECV_TMO_MS, OTA_HTTP_SEND_TMO_MS );
        if( st != TLS_TRANSPORT_SUCCESS )
        {
            log_error( "[OTA] TLS connect to %s:%u failed (%d)", host, port, (int) st );
            return -1;
        }
        return 0;
    }
    else
    {
        struct freertos_sockaddr addr;
        uint32_t ip;
        TickType_t rt = pdMS_TO_TICKS( OTA_HTTP_RECV_TMO_MS ), wt = pdMS_TO_TICKS( OTA_HTTP_SEND_TMO_MS );

        /* A dotted-decimal literal (e.g. a LAN OTA server) resolves directly - and must, since a
         * DNS-less LAN can't look up a hostname. FreeRTOS_inet_addr returns 0 for a non-literal. */
        ip = FreeRTOS_inet_addr( host );
        if( ip == 0 )
        {
#ifdef AT_PROJECT_CARES
            struct freertos_sockaddr fa;
            ip = ( cares_resolve( host, FREERTOS_AF_INET, &fa ) == 0 ) ? fa.sin_address.ulIP_IPv4 : 0U;
#else
            ip = FreeRTOS_gethostbyname( host );
#endif
        }
        if( ip == 0 )
        {
            log_error( "[OTA] DNS lookup of %s failed", host );
            return -1;
        }
        c->sock = FreeRTOS_socket( FREERTOS_AF_INET, FREERTOS_SOCK_STREAM, FREERTOS_IPPROTO_TCP );
        if( c->sock == FREERTOS_INVALID_SOCKET )
        {
            log_error( "[OTA] socket() failed" );
            return -1;
        }
        FreeRTOS_setsockopt( c->sock, 0, FREERTOS_SO_RCVTIMEO, &rt, sizeof( rt ) );
        FreeRTOS_setsockopt( c->sock, 0, FREERTOS_SO_SNDTIMEO, &wt, sizeof( wt ) );
        memset( &addr, 0, sizeof( addr ) );
        addr.sin_family = FREERTOS_AF_INET;
        addr.sin_port   = FreeRTOS_htons( port );
        addr.sin_address.ulIP_IPv4 = ip;
        if( FreeRTOS_connect( c->sock, &addr, sizeof( addr ) ) != 0 )
        {
            log_error( "[OTA] TCP connect to %s:%u failed", host, port );
            FreeRTOS_closesocket( c->sock );
            c->sock = NULL;
            return -1;
        }
        return 0;
    }
}

static int conn_send( conn_t * c, const void * buf, size_t len )
{
    if( c->is_https )
        return (int) TLS_FreeRTOS_send( c->tls, buf, len );
    return (int) FreeRTOS_send( c->sock, buf, len, 0 );
}

static int conn_recv( conn_t * c, void * buf, size_t len )
{
    if( c->is_https )
        return (int) TLS_FreeRTOS_recv( c->tls, buf, len );
    return (int) FreeRTOS_recv( c->sock, buf, len, 0 );
}

static void conn_close( conn_t * c )
{
    if( c->is_https )
    {
        if( c->tls )
            TLS_FreeRTOS_Disconnect( c->tls );
    }
    else if( c->sock )
    {
        FreeRTOS_shutdown( c->sock, FREERTOS_SHUT_RDWR );
        FreeRTOS_closesocket( c->sock );
    }
}

/* ---- llhttp streaming sink ------------------------------------------------------------------- */
typedef struct
{
    ota_ctx_t ota;
    int       status;        /* HTTP status code        */
    int       err;           /* <0 on flash/abort error */
    int       started;       /* ota_flash_begin done    */
    int       done;          /* message complete        */
} sink_t;

static int cb_status( llhttp_t * p, const char * at, size_t len )
{
    ( void ) at; ( void ) len;
    ( (sink_t *) p->data )->status = llhttp_get_status_code( p );
    return 0;
}

static int cb_body( llhttp_t * p, const char * at, size_t len )
{
    sink_t * s = (sink_t *) p->data;
    if( s->status != 200 )
        return 0;                       /* not OK - don't write the error page to flash */
    if( ota_flash_feed( &s->ota, (const uint8_t *) at, (uint32_t) len ) < 0 )
    {
        s->err = -1;
        return -1;                      /* abort llhttp_execute */
    }
    return 0;
}

static int cb_complete( llhttp_t * p )
{
    ( (sink_t *) p->data )->done = 1;
    return 0;
}

static void ota_http_fire_fail_hook( void );   /* one-shot failure hook (defined with ota_http_start) */

/* ---- the OTA worker -------------------------------------------------------------------------- */
static void ota_http_task( void * param )
{
    char *   url = (char *) param;
    size_t   urllen = strlen( url );
    urlparse_url u;
    char     host[ 128 ];
    uint16_t port;
    int      is_https;
    conn_t   conn;
    sink_t   sink;
    llhttp_t parser;
    llhttp_settings_t settings;
    /* big buffers are static (one OTA at a time) to leave the task stack free for the TLS handshake */
    static char    path[ OTA_PATH_BUF ];
    static uint8_t reqbuf[ OTA_REQ_BUF ];
    static char    rbuf[ OTA_RECV_BUF ];
    HTTPRequestInfo_t   reqInfo;
    HTTPRequestHeaders_t reqHdr;

    memset( &sink, 0, sizeof( sink ) );

    /* PSA crypto backs picotls/mbedTLS RNG + the whole handshake. ota_http may be the first TLS user
     * (e.g. AWS never connected), so ensure PSA is up or the handshake fails (recv -6 / SSL internal
     * error). Idempotent - a no-op if the AWS path already initialised it. */
    ( void ) psa_crypto_init();

    /* 1. parse the URL */
    if( urlparse_parse_url( url, urllen, 0, &u ) != 0 || !( u.field_set & ( 1 << URLPARSE_HOST ) ) )
    {
        log_error( "[OTA] bad URL: %s", url );
        goto out;
    }
    {
        size_t hl = u.field_data[ URLPARSE_HOST ].len;
        size_t ho = u.field_data[ URLPARSE_HOST ].off;
        if( hl == 0 || hl >= sizeof( host ) ) { log_error( "[OTA] host too long" ); goto out; }
        memcpy( host, url + ho, hl );
        host[ hl ] = '\0';
    }
    is_https = ( u.field_set & ( 1 << URLPARSE_SCHEMA ) ) &&
               u.field_data[ URLPARSE_SCHEMA ].len == 5 &&
               strncmp( url + u.field_data[ URLPARSE_SCHEMA ].off, "https", 5 ) == 0;
    port = ( u.field_set & ( 1 << URLPARSE_PORT ) ) ? u.port : ( is_https ? 443 : 80 );
    /* request-target = path + query (everything after the authority, fragment dropped) */
    if( u.field_set & ( 1 << URLPARSE_PATH ) )
    {
        size_t po = u.field_data[ URLPARSE_PATH ].off;
        size_t pl = urllen - po;                       /* path + ?query (+#frag) */
        char * frag = memchr( url + po, '#', pl );
        if( frag )
            pl = (size_t) ( frag - ( url + po ) );
        if( pl >= sizeof( path ) ) { log_error( "[OTA] path too long" ); goto out; }
        memcpy( path, url + po, pl );
        path[ pl ] = '\0';
    }
    else
    {
        path[ 0 ] = '/'; path[ 1 ] = '\0';
    }
    log_info( "[OTA] %s host=%s port=%u path=%s", is_https ? "https" : "http", host, port, path );

    /* 2. anti-conflict: refuse if a BLE/AWS OTA owns the slot, then detect the target slot */
    if( ota_lock_try( OTA_SRC_HTTP ) != 0 )
    {
        log_error( "[OTA] rejected - a BLE/AWS OTA is in progress" );
        goto out;
    }
    if( ota_flash_begin( &sink.ota ) < 0 )
        goto out;
    sink.started = 1;

    /* 3. connect - retry a few times so a transient first-resolve DNS timeout (fresh S3 host, no cache)
     * or a picotls recv -6 doesn't abort the whole OTA (the AWS MQTT path retries the same way). */
    {
        unsigned tries;
        int      ok = 0;

        for( tries = 0; tries < OTA_CONNECT_RETRIES; tries++ )
        {
            if( conn_connect( &conn, is_https, host, port ) >= 0 )
            {
                ok = 1;
                break;
            }
            log_error( "[OTA] connect attempt %u/%u failed - retrying", tries + 1u, OTA_CONNECT_RETRIES );
            conn_close( &conn );                 /* drop any half-open state before the next attempt */
            vTaskDelay( pdMS_TO_TICKS( 1500 ) );
        }
        if( !ok )
            goto out;
    }

    /* 4. build the GET request with coreHTTP, then send it */
    memset( &reqInfo, 0, sizeof( reqInfo ) );
    reqInfo.pMethod  = "GET";        reqInfo.methodLen = 3;
    reqInfo.pPath    = path;         reqInfo.pathLen   = strlen( path );
    reqInfo.pHost    = host;         reqInfo.hostLen   = strlen( host );
    reqInfo.reqFlags = 0;            /* no keep-alive -> coreHTTP adds "Connection: close" */
    memset( &reqHdr, 0, sizeof( reqHdr ) );
    reqHdr.pBuffer   = reqbuf;
    reqHdr.bufferLen = sizeof( reqbuf );
    if( HTTPClient_InitializeRequestHeaders( &reqHdr, &reqInfo ) != HTTPSuccess )
    {
        log_error( "[OTA] failed to build request headers" );
        goto close;
    }
    if( conn_send( &conn, reqHdr.pBuffer, reqHdr.headersLen ) != (int) reqHdr.headersLen )
    {
        log_error( "[OTA] sending request failed" );
        goto close;
    }

    /* 5. stream the response through llhttp; on_body writes each chunk to flash */
    llhttp_settings_init( &settings );
    settings.on_status           = cb_status;
    settings.on_body             = cb_body;
    settings.on_message_complete = cb_complete;
    llhttp_init( &parser, HTTP_RESPONSE, &settings );
    parser.data = &sink;

    for( ; ; )
    {
        int r = conn_recv( &conn, rbuf, sizeof( rbuf ) );
        if( r > 0 )
        {
            llhttp_errno_t e = llhttp_execute( &parser, rbuf, (size_t) r );
            if( e != HPE_OK )
            {
                if( sink.err == 0 )
                    log_error( "[OTA] HTTP parse error: %s", llhttp_errno_name( e ) );
                break;
            }
            if( sink.done )
                break;
        }
        else if( r == 0 )
        {
            continue;                       /* recv timeout - keep waiting */
        }
        else
        {
            /* peer closed (Connection: close after the body) or error */
            llhttp_finish( &parser );
            break;
        }
    }

close:
    conn_close( &conn );
out:
#if ( INCLUDE_uxTaskGetStackHighWaterMark == 1 )
    /* Stack high-water (in WORDS still free) - the peak was the TLS handshake. Use it to right-size
     * the task stack below: free_words ~= headroom we can reclaim. (Diagnostic; skipped where the
     * FreeRTOS config doesn't build uxTaskGetStackHighWaterMark, e.g. bl602.) */
    log_info( "[OTA] task stack: %u words still free (high-water)",
              (unsigned) uxTaskGetStackHighWaterMark( NULL ) );
#endif

    if( sink.status != 0 && sink.status != 200 )
        log_error( "[OTA] server returned HTTP %d", sink.status );

    if( sink.err == 0 && sink.started && sink.ota.have_hdr &&
        ota_flash_finish( &sink.ota ) == 0 )
    {
        log_info( "[OTA] update OK" );
        vPortFree( url );
        ota_flash_reboot();                 /* does not return */
    }

    ota_lock_release( OTA_SRC_HTTP );
    log_error( "[OTA] update FAILED" );
    vPortFree( url );
    ota_http_fire_fail_hook();
    vTaskDelete( NULL );
}

/* ---- programmatic trigger -------------------------------------------------------------------- *
 * Launch the async downloader for `url` (a private copy is made). Used both by the console command
 * and by the AWS OTA path (ota_app.c prvOtaHttpInit hands off the job-doc presigned URL here). The
 * worker streams OTA_All.bin to the inactive slot and reboots on success; on failure it just logs
 * and exits (no reboot). Non-blocking: returns once the 12 KB task is created. */

/* One-shot failure hook (see ota_http.h). Consumed before firing so a hook that reboots (the AWS
 * takeover) or re-arms can't double-fire. */
static void ( * s_fail_hook )( void );

void ota_http_set_fail_hook( void ( * hook )( void ) )
{
    s_fail_hook = hook;
}

static void ota_http_fire_fail_hook( void )
{
    void ( * hook )( void ) = s_fail_hook;

    s_fail_hook = NULL;
    if( hook != NULL )
    {
        hook();
    }
}

void ota_http_start( const char * url )
{
    size_t len;
    char * copy;

    if( url == NULL )
    {
        return;
    }
    len = strlen( url );
    copy = pvPortMalloc( len + 1 );
    if( copy == NULL )
    {
        log_error( "[OTA] ota_http_start: out of memory" );
        ota_http_fire_fail_hook();
        return;
    }
    memcpy( copy, url, len + 1 );

    /* 3072 words (12 KB): measured TLS-handshake high-water is ~1941 words (~7.8 KB), so this leaves
     * ~1131 words (~4.5 KB) headroom while reclaiming ~4 KB of heap vs the old 4096. */
    if( xTaskCreate( ota_http_task, "ota_http", 3072, copy, tskIDLE_PRIORITY + 3, NULL ) != pdPASS )
    {
        vPortFree( copy );
        log_error( "[OTA] ota_http_start: failed to create task" );
        ota_http_fire_fail_hook();
    }
}

/* ---- console command ------------------------------------------------------------------------- */
static BaseType_t cmd_ota_http( char * out, size_t outlen, const char * cmd )
{
    const char * arg;
    BaseType_t   arglen;
    char *       url;

    arg = FreeRTOS_CLIGetParameter( cmd, 1, &arglen );
    if( arg == NULL || arglen <= 0 )
    {
        snprintf( out, outlen, "usage: ota_http <http(s)://host/path-to-OTA_All.bin>\r\n" );
        return pdFALSE;
    }
    url = pvPortMalloc( (size_t) arglen + 1 );
    if( url == NULL )
    {
        snprintf( out, outlen, "ota_http: out of memory\r\n" );
        return pdFALSE;
    }
    memcpy( url, arg, (size_t) arglen );
    url[ arglen ] = '\0';

    ota_http_start( url );
    vPortFree( url );   /* ota_http_start made its own copy */
    snprintf( out, outlen, "ota_http: downloading - watch the log; the device reboots on success\r\n" );
    return pdFALSE;
}

void ota_http_register( void )
{
    static const CLI_Command_Definition_t def =
    {
        "ota_http",
        "ota_http <url>:\r\n Download an OTA_All.bin over HTTP/HTTPS and apply it (reboots on success)\r\n",
        cmd_ota_http,
        1
    };
    FreeRTOS_CLIRegisterCommand( &def );
}
