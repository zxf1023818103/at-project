/*
 * ota_app.c - AWS IoT OTA Agent wiring for AmebaD KM4 (make AWS=ON).
 *
 * Connects the FreeRTOS OTA Agent (v3.3.0) to the shared coreMQTT-Agent connection:
 *  - OS interface  : the shipped ota_os_freertos.c (event queue + timers + malloc).
 *  - MQTT interface: subscribe/unsubscribe/publish wrappers that enqueue MQTTAgent_*
 *                    commands and BLOCK on a task-notification until the agent task's
 *                    command-complete callback fires. The subscribe wrapper also
 *                    registers an incoming-publish callback in the subscription_manager
 *                    keyed by the topic filter; that callback copies the payload into an
 *                    OtaEventData_t and OTA_SignalEvent()s the agent (job doc vs file
 *                    block, distinguished by the "/streams/" topic segment).
 *  - PAL interface : ota_pal.c (slot write via ota_flash.c + mbedTLS signature verify).
 *
 * The OTA agent runs in its own task (OTA_EventProcessingTask). On the first MQTT-Agent
 * connection, ota_app_init() OTA_Init()s and starts the task; ota_app_on_mqtt_connected()
 * signals OtaAgentEventStart (or a fresh OTA_CheckForUpdate after a reconnect) so the job
 * subscribe/check rides the live session. The subscription_manager re-registers the OTA
 * topic callbacks automatically because the OTA agent re-subscribes after a resume.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "ota.h"
#include "ota_private.h"
#include "ota_os_freertos.h"
#include "ota_appversion32.h"

#include "core_mqtt_agent.h"
#include "subscription_manager.h"

#include "at_log.h"                 /* rxi log.h enum must precede transport_tls.h's logging_levels.h LOG_* macros (mbedtls/fallback backend) */

/* HTTP data plane is default-on (AmebaD: presigned-S3 download over a 2nd HTTPS TLS). BL602's
 * MQTT-only build (~68 KB heap can't hold a 2nd TLS) sets OTA_APP_ENABLE_HTTP=0: the OTA agent
 * then downloads file blocks over the shared MQTT/CBOR data plane (single TLS reused from
 * telemetry), and none of coreHTTP / transport_tls / cares / urlparse is pulled in. */
#ifndef OTA_APP_ENABLE_HTTP
#define OTA_APP_ENABLE_HTTP    1
#endif

#if OTA_APP_ENABLE_HTTP
/* HTTP data plane (presigned-S3 download over HTTPS): coreHTTP request builder + Send/parse,
 * the shared TLS transport (picotls/mbedTLS), plain FreeRTOS+TCP for http://, and the vendored
 * URL parser. Mirrors ota_http.c's transport setup; here it feeds the OTA agent file blocks. */
#include "core_http_client.h"
#include "transport_tls.h"          /* TLS_FreeRTOS_*, NetworkCredentials_t, TlsTransportParams_t */
#include "transport_interface.h"
#include "urlparse.h"

#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#ifdef AT_PROJECT_CARES
#include "cares_resolve.h"
#endif
#include "FreeRTOS_DNS.h"

#include "ota_http_interface.h"
#endif /* OTA_APP_ENABLE_HTTP */

#include "ota_app.h"

/* Use the app's flushed at_log (printf is fully-buffered per-task on the LOGUART, so [OTA] printf
 * lines never surface). The first vararg is always a string literal, so the "[OTA] " concat works. */
#define OTA_APP_LOG( ... )    log_info( "[OTA] " __VA_ARGS__ )

#if OTA_APP_ENABLE_HTTP
/* The TLS transport leaves struct NetworkContext incomplete; each consumer defines it (ota_http.c /
 * the AWS demo do the same). It holds just the TlsTransportParams_t the transport functions use. */
struct NetworkContext
{
    TlsTransportParams_t * pParams;
};
#endif

/* The PAL interface getter (ota_pal.c). */
extern const OtaPalInterface_t * ota_pal_get_interface( void );

/* The OTA library compares this against the job's version to reject downgrades / detect a
 * same-version self-test. ota.c declares it extern; the app must define it. Bump the build
 * field for each OTA-B image so the cloud-side version gate works. (The human-readable
 * AT_FW_VERSION string stamped into telemetry is independent of this numeric triple.) */
#ifndef APP_VERSION_MAJOR
#define APP_VERSION_MAJOR    0
#endif
#ifndef APP_VERSION_MINOR
#define APP_VERSION_MINOR    9
#endif
#ifndef APP_VERSION_BUILD
#define APP_VERSION_BUILD    2
#endif
const AppVersion32_t appFirmwareVersion =
{
    .u.x.major = APP_VERSION_MAJOR,
    .u.x.minor = APP_VERSION_MINOR,
    .u.x.build = APP_VERSION_BUILD,
};

/* True while an OTA file download is in progress (CreatingFile..ClosingFile). The telemetry task
 * uses this to go quiet so its publish churn doesn't fragment the heap under the OTA HTTPS TLS. */
int ota_app_is_active( void )
{
    OtaState_t st = OTA_GetState();
    return ( ( st > OtaAgentStateWaitingForJob ) && ( st < OtaAgentStateSuspended ) ) ? 1 : 0;
}

/* ------------------------------------------------------------- buffers -------- */
/* OtaAppBuffer_t static backing (sizes per the task brief). */
static uint8_t s_updateFilePath[ 256 ];
static uint8_t s_certFilePath[ 256 ];
static uint8_t s_streamName[ 64 ];
static uint8_t s_fileBitmap[ OTA_MAX_BLOCK_BITMAP_SIZE ]; /* 128 */
#ifdef AWS_STATIC_TASKS
/* Heap-tight (bl602): the decode buffer is only used AFTER the MQTT connect, so heap-allocate it in
 * ota_app_init() (post-handshake) instead of reserving OTA_FILE_BLOCK_SIZE of .bss that would shrink
 * the heap the TLS handshake's 6 KB IN buffer must fit into. */
static uint8_t * s_decodeMem;
#else
static uint8_t s_decodeMem[ OTA_FILE_BLOCK_SIZE ];     /* otaconfigLOG2_FILE_BLOCK_SIZE=12 -> 4096 */
#endif
#if OTA_APP_ENABLE_HTTP
/* HTTP data plane: the OTA library copies the job's presigned-S3 URL here (pUpdateUrlPath) and the
 * "Connect:" auth scheme there. SigV4 presigned URLs run ~500-1500 chars (OTA_REQUEST_URL_MAX_SIZE). */
static char s_otaUrl[ 1536 ];
static char s_otaAuthScheme[ 48 ];
#endif

/* OTA event-data buffer pool: the MQTT incoming-publish callback wraps a received
 * job-doc / file-block payload in one of these before OTA_SignalEvent(). The OTA agent
 * marks bufferUsed=false again once processed; we recycle from this pool. */
/* 1 buffer (each is OTA_DATA_BLOCK_SIZE = 4096+1500+30 = 5.6 KB of .bss): the HTTP data plane opens a
 * SECOND TLS context alongside the AWS MQTT TLS and the heap is very tight, so we trade buffers for
 * heap. HTTP is fully sequential (http.request fetches + feeds ONE block, the agent processes it,
 * recycles, then the next request) and the job-doc phase precedes the data phase, so one buffer
 * round-trips cleanly. (Dropped from 2 to free ~5.6 KB for the OTA HTTPS handshake working set.) */
#ifndef OTA_APP_NUM_EVENT_BUFFERS
#define OTA_APP_NUM_EVENT_BUFFERS    ( 1U )
#endif
#ifdef AWS_STATIC_TASKS
static OtaEventData_t * s_eventBuffers;   /* heap-allocated post-handshake (bl602 heap; see s_decodeMem) */
#else
static OtaEventData_t s_eventBuffers[ OTA_APP_NUM_EVENT_BUFFERS ];
#endif
static SemaphoreHandle_t s_bufferMutex = NULL;
static StaticSemaphore_t s_bufferMutexStorage;

/* Shared MQTT-Agent context (owned by aws_iot_demo.c). */
static MQTTAgentContext_t * s_pAgent = NULL;

/* The OTA Agent interfaces (must stay in scope for the life of the agent). */
static OtaInterfaces_t s_otaInterfaces;
static OtaAppBuffer_t  s_otaBuffer;

static int s_initialised = 0;

/* ------------------------------------------------------- event-buffer pool ---- */
static OtaEventData_t * prvEventBufferGet( void )
{
    OtaEventData_t * pBuf = NULL;
    uint32_t i;

    if( s_bufferMutex == NULL )
    {
        return NULL;
    }

    if( xSemaphoreTake( s_bufferMutex, portMAX_DELAY ) == pdTRUE )
    {
        for( i = 0; i < OTA_APP_NUM_EVENT_BUFFERS; i++ )
        {
            if( s_eventBuffers[ i ].bufferUsed == false )
            {
                s_eventBuffers[ i ].bufferUsed = true;
                pBuf = &s_eventBuffers[ i ];
                break;
            }
        }
        ( void ) xSemaphoreGive( s_bufferMutex );
    }

    return pBuf;
}

#if OTA_APP_ENABLE_HTTP
/* ============================================================ HTTP data plane ==
 * Download file blocks over HTTPS (or HTTP) from the presigned-S3 URL in the job document,
 * instead of the MQTT/CBOR data plane. The OTA library drives this via the OtaHttpInterface_t:
 *   init(pUrl)              - parse the URL + open the connection (kept open for the transfer).
 *   request(start, end)     - GET the byte range, expect 206, feed the BODY to the agent.
 *   deinit()               - close + reset.
 *
 * Data-feed mechanism (confirmed against decodeFileBlock_Http / ota.c processDataHandler):
 *   the agent passes pEventData->data + pEventData->dataLength straight to decodeFileBlock_Http,
 *   which just memcpy()s the raw bytes (no CBOR decode). So request() copies the raw 206 response
 *   body into a prvEventBufferGet() OtaEventData_t, sets dataLength, eventId =
 *   OtaAgentEventReceivedFileBlock, and OTA_SignalEvent()s - identical to the MQTT path's
 *   prvOtaIncomingPublish(), but with the raw HTTP body in place of the stream-CBOR payload. */

static OtaHttpStatus_t prvOtaHttpDeinit( void );

#define OTA_HTTP_RECV_TMO_MS    15000u
#define OTA_HTTP_SEND_TMO_MS    10000u
/* rxPlain reserve for the OTA HTTPS connection. Must match the working ota_http.c (16640): S3's
 * handshake Certificate record is larger than a 6 KB reserve, and pinning it too small fails the
 * handshake (err 4). Heap headroom for this 2nd TLS comes from the freed event buffers + the AWS
 * MQTT TLS now reserving only 8 KB. */
/* 5120: holds one decrypted S3 application record = ~400-500 B 206 headers + a 4 KB body chunk
 * (otaconfigLOG2_FILE_BLOCK_SIZE=12), with ~0.5 KB margin, without a mid-transfer realloc. Trimmed
 * from 8192 to cut ~3 KB of steady heap held by this (2nd) picotls connection while the AWS MQTT TLS
 * is live - the binding constraint is that concurrent peak. (ptls_buffer grows 2x on demand and never
 * shrinks, so if S3 ever coalesces a record beyond 5 KB this just costs one realloc, not correctness;
 * verified against the AT_PROJECT_TLS_HEAP_TRACE handshake trace.) Larger blocks were MEASURED to OOM the
 * S3 handshake: 8 KB blocks @9216 (~69 KB idle) and 16 KB @16640 (~44.5 KB idle). This connection is
 * forced to picotls (TLS 1.3) - see creds.backendMode below. */
#define OTA_HTTP_RXRESERVE      5120u
/* coreHTTP response buffer: holds the WHOLE response (coreHTTP is not streaming) = 4 KB block +
 * status line + headers. Measured S3 206 headers are only ~480 B, so 1 KB of header room is ample
 * (the earlier HTTPPartialResponse was the body not arriving, not header overflow). Keep it lean to
 * leave heap for the 2nd TLS. */
#define OTA_HTTP_RESP_BUF       ( OTA_FILE_BLOCK_SIZE + 1024u )
/* Request header buffer: the request LINE alone is "GET <path> HTTP/1.1" and the S3 presigned-URL
 * path carries the full SigV4 query (~1000+ chars), so 512 overflows (HTTPInsufficientMemory). Size
 * it to hold the long path + Host + Range + Connection. */
#define OTA_HTTP_REQ_BUF        2048u
#define OTA_HTTP_PATH_BUF       1408u           /* path + ?query (SigV4 presigned URLs are long) */
/* In-call reconnect-retry budget: S3 closes the keep-alive every few ranged GETs (206 empty-body /
 * HTTPClient_Send fail). Reconnecting right here (~0.5 s) and retrying the same range beats returning
 * failure and idling the OTA agent's otaconfigFILE_REQUEST_WAIT_MS (10 s) RequestTimer. Cap so a truly
 * dead network still surfaces (then the agent's timer takes over). */
#define OTA_HTTP_REQ_RETRIES    6u
#define OTA_HTTP_HOST_BUF       128u

/* Amazon Root CA 1 - the trust anchor for *.s3*.amazonaws.com (and most AWS endpoints).
 * (Same anchor ota_http.c uses for the console `ota_http` command.) */
static const char OTA_AMAZON_ROOT_CA1[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\n"
    "ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\n"
    "b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\n"
    "MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\n"
    "b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\n"
    "ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\n"
    "9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\n"
    "IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\n"
    "VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\n"
    "93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\n"
    "jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\n"
    "AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\n"
    "A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\n"
    "U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs\n"
    "N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv\n"
    "o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU\n"
    "5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy\n"
    "rqXRfboQnoZsG4q5WTP468SQvvG5\n"
    "-----END CERTIFICATE-----\n";

/* Open connection state (one OTA at a time). */
static struct
{
    int                  inUse;
    int                  isHttps;
    /* https: */
    NetworkContext_t     netCtx;
    TlsTransportParams_t tlsParams;
    /* http: */
    Socket_t             sock;
    /* parsed target */
    char                 host[ OTA_HTTP_HOST_BUF ];
    char                 path[ OTA_HTTP_PATH_BUF ];
    uint16_t             port;
} s_http;

/* TransportInterface_t glue: route coreHTTP's send/recv to the open connection (TLS or TCP). */
static int32_t prvHttpTransportSend( NetworkContext_t * pCtx, const void * pBuf, size_t len )
{
    ( void ) pCtx;
    if( s_http.isHttps )
    {
        return TLS_FreeRTOS_send( &s_http.netCtx, pBuf, len );
    }
    return ( int32_t ) FreeRTOS_send( s_http.sock, pBuf, len, 0 );
}

static int32_t prvHttpTransportRecv( NetworkContext_t * pCtx, void * pBuf, size_t len )
{
    ( void ) pCtx;
    if( s_http.isHttps )
    {
        return TLS_FreeRTOS_recv( &s_http.netCtx, pBuf, len );
    }
    return ( int32_t ) FreeRTOS_recv( s_http.sock, pBuf, len, 0 );
}

/* Parse pUrl into s_http.host / s_http.path / s_http.port / s_http.isHttps. 0 on success. */
static int prvHttpParseUrl( const char * pUrl )
{
    urlparse_url u;
    size_t urllen = strlen( pUrl );
    int isHttps;
    uint16_t port;

    if( ( urlparse_parse_url( pUrl, urllen, 0, &u ) != 0 ) ||
        !( u.field_set & ( 1 << URLPARSE_HOST ) ) )
    {
        OTA_APP_LOG( "http: bad URL" );
        return -1;
    }

    {
        size_t hl = u.field_data[ URLPARSE_HOST ].len;
        size_t ho = u.field_data[ URLPARSE_HOST ].off;
        if( ( hl == 0 ) || ( hl >= sizeof( s_http.host ) ) )
        {
            OTA_APP_LOG( "http: host too long" );
            return -1;
        }
        memcpy( s_http.host, pUrl + ho, hl );
        s_http.host[ hl ] = '\0';
    }

    isHttps = ( u.field_set & ( 1 << URLPARSE_SCHEMA ) ) &&
              ( u.field_data[ URLPARSE_SCHEMA ].len == 5 ) &&
              ( strncmp( pUrl + u.field_data[ URLPARSE_SCHEMA ].off, "https", 5 ) == 0 );
    port = ( u.field_set & ( 1 << URLPARSE_PORT ) ) ? u.port : ( isHttps ? 443 : 80 );

    if( u.field_set & ( 1 << URLPARSE_PATH ) )
    {
        size_t po = u.field_data[ URLPARSE_PATH ].off;
        size_t pl = urllen - po;                       /* path + ?query (+#frag) */
        char * frag = memchr( pUrl + po, '#', pl );
        if( frag != NULL )
        {
            pl = ( size_t ) ( frag - ( pUrl + po ) );
        }
        if( pl >= sizeof( s_http.path ) )
        {
            OTA_APP_LOG( "http: path too long" );
            return -1;
        }
        memcpy( s_http.path, pUrl + po, pl );
        s_http.path[ pl ] = '\0';
    }
    else
    {
        s_http.path[ 0 ] = '/';
        s_http.path[ 1 ] = '\0';
    }

    s_http.isHttps = isHttps;
    s_http.port = port;
    return 0;
}

/* Defined by component/nimble_config/nimble_app.c only in NIMBLE builds; weak so AWS-only images
 * (no NimBLE) still link (resolves to NULL -> skipped). Tearing BLE down frees ~7 KB for the HTTPS
 * data-plane TLS. NOTE: on AWS+NIMBLE this ~7 KB is NOT enough on its own -- the HTTPS OTA still
 * OOMs (two concurrent TLS contexts exceed the heap); kept as a (harmless) partial mitigation. */
extern void nimble_stop( void ) __attribute__( ( weak ) );

/* OtaHttpInterface_t.init: parse the presigned URL and open the connection. */
/* (Re)open the transport to the already-parsed s_http.host:port. Used by init AND by request()'s
 * self-healing reconnect (S3 drops the keep-alive after a few hundred KB of ranged GETs). On success
 * s_http.inUse=1; on failure the partial state is freed (leak-safe) and inUse stays 0. */
static OtaHttpStatus_t prvHttpOpenConn( void )
{
    if( s_http.isHttps )
    {
        NetworkCredentials_t creds;

        /* Free BLE heap before opening the OTA data-plane TLS (partial mitigation; see note above). */
        if( nimble_stop != NULL )
        {
            nimble_stop();
        }

        memset( &creds, 0, sizeof( creds ) );
        creds.pRootCa        = ( const uint8_t * ) OTA_AMAZON_ROOT_CA1;
        creds.rootCaSize     = sizeof( OTA_AMAZON_ROOT_CA1 );   /* incl. NUL, as mbedTLS PEM wants */
        creds.rxReserveSize  = OTA_HTTP_RXRESERVE;
        creds.backendMode    = TLS_BACKEND_MODE_PICOTLS;   /* AWS S3 supports TLS 1.3; never fall back */
        s_http.netCtx.pParams = &s_http.tlsParams;

        if( TLS_FreeRTOS_Connect( &s_http.netCtx, s_http.host, s_http.port, &creds,
                                  OTA_HTTP_RECV_TMO_MS, OTA_HTTP_SEND_TMO_MS ) != TLS_TRANSPORT_SUCCESS )
        {
            OTA_APP_LOG( "http: TLS connect to %s:%u FAIL", s_http.host, ( unsigned ) s_http.port );
            /* CRITICAL: free what TLS_FreeRTOS_Connect allocated before failing - a transient
             * handshake failure (recv -6) otherwise leaks ~tens of KB and the retry OOMs. */
            TLS_FreeRTOS_Disconnect( &s_http.netCtx );
            return OtaHttpInitFailed;
        }
    }
    else
    {
        struct freertos_sockaddr addr;
        uint32_t ip;
        TickType_t rt = pdMS_TO_TICKS( OTA_HTTP_RECV_TMO_MS );
        TickType_t wt = pdMS_TO_TICKS( OTA_HTTP_SEND_TMO_MS );

#ifdef AT_PROJECT_CARES
        {
            struct freertos_sockaddr fa;
            ip = ( cares_resolve( s_http.host, FREERTOS_AF_INET, &fa ) == 0 ) ? fa.sin_address.ulIP_IPv4 : 0U;
        }
#else
        ip = FreeRTOS_gethostbyname( s_http.host );
#endif
        if( ip == 0 )
        {
            OTA_APP_LOG( "http: DNS lookup of %s FAIL", s_http.host );
            return OtaHttpInitFailed;
        }
        s_http.sock = FreeRTOS_socket( FREERTOS_AF_INET, FREERTOS_SOCK_STREAM, FREERTOS_IPPROTO_TCP );
        if( s_http.sock == FREERTOS_INVALID_SOCKET )
        {
            OTA_APP_LOG( "http: socket() FAIL" );
            s_http.sock = NULL;
            return OtaHttpInitFailed;
        }
        FreeRTOS_setsockopt( s_http.sock, 0, FREERTOS_SO_RCVTIMEO, &rt, sizeof( rt ) );
        FreeRTOS_setsockopt( s_http.sock, 0, FREERTOS_SO_SNDTIMEO, &wt, sizeof( wt ) );
        memset( &addr, 0, sizeof( addr ) );
        addr.sin_family = FREERTOS_AF_INET;
        addr.sin_port   = FreeRTOS_htons( s_http.port );
        addr.sin_address.ulIP_IPv4 = ip;
        if( FreeRTOS_connect( s_http.sock, &addr, sizeof( addr ) ) != 0 )
        {
            OTA_APP_LOG( "http: TCP connect to %s:%u FAIL", s_http.host, ( unsigned ) s_http.port );
            FreeRTOS_closesocket( s_http.sock );
            s_http.sock = NULL;
            return OtaHttpInitFailed;
        }
    }

    s_http.inUse = 1;
    return OtaHttpSuccess;
}

/* Close the transport but KEEP the parsed s_http.host/path/port so request() can reopen. */
static void prvHttpCloseConn( void )
{
    if( s_http.isHttps )
    {
        TLS_FreeRTOS_Disconnect( &s_http.netCtx );
    }
    else if( s_http.sock != NULL )
    {
        FreeRTOS_closesocket( s_http.sock );
        s_http.sock = NULL;
    }
    s_http.inUse = 0;
}

static OtaHttpStatus_t prvOtaHttpInit( char * pUrl )
{
    if( ( pUrl == NULL ) || ( pUrl[ 0 ] == '\0' ) )
    {
        return OtaHttpInitFailed;
    }

    if( s_http.inUse )
    {
        /* A stale connection from an aborted job; close it before reopening. */
        ( void ) prvOtaHttpDeinit();
    }

    memset( &s_http, 0, sizeof( s_http ) );

    if( prvHttpParseUrl( pUrl ) != 0 )
    {
        return OtaHttpInitFailed;
    }

    OTA_APP_LOG( "http: %s host=%s port=%u", s_http.isHttps ? "https" : "http",
                 s_http.host, ( unsigned ) s_http.port );

    return prvHttpOpenConn();
}

/* Millisecond clock for coreHTTP's recv-retry budget (HTTPResponse_t.getTime). */
static uint32_t prvHttpGetTimeMs( void )
{
    return ( uint32_t ) ( xTaskGetTickCount() * portTICK_PERIOD_MS );
}

/* OtaHttpInterface_t.request: GET [rangeStart,rangeEnd], expect 206, feed the body to the agent. */
static OtaHttpStatus_t prvOtaHttpRequest( uint32_t rangeStart, uint32_t rangeEnd )
{
    TransportInterface_t transport;
    HTTPRequestInfo_t    reqInfo;
    HTTPRequestHeaders_t reqHdr;
    HTTPResponse_t       response;
    HTTPStatus_t         hs;
    static uint8_t       s_reqBuf[ OTA_HTTP_REQ_BUF ];
    static uint8_t       s_respBuf[ OTA_HTTP_RESP_BUF ];
    OtaEventMsg_t        eventMsg = { 0 };
    OtaEventData_t *     pData;
    unsigned             attempt;

    /* In-call reconnect-retry loop (see OTA_HTTP_REQ_RETRIES): S3 closes the keep-alive every few
     * ranged GETs - HTTPClient_Send then fails, or returns 206 with an empty body. Instead of
     * returning failure and letting the OTA agent idle the 10 s otaconfigFILE_REQUEST_WAIT_MS
     * RequestTimer before re-requesting, we reconnect right here (~0.5 s) and retry the SAME range. */
    for( attempt = 0; attempt < OTA_HTTP_REQ_RETRIES; attempt++ )
    {
        /* (Re)open the connection at the top of each attempt if it's down. */
        if( !s_http.inUse )
        {
            if( prvHttpOpenConn() != OtaHttpSuccess )
            {
                continue;   /* reopen failed - retry (or exhaust the budget) */
            }
        }

        /* Build the GET request with coreHTTP, add the byte Range header. */
        memset( &reqInfo, 0, sizeof( reqInfo ) );
        reqInfo.pMethod   = "GET";          reqInfo.methodLen = 3;
        reqInfo.pPath     = s_http.path;    reqInfo.pathLen   = strlen( s_http.path );
        reqInfo.pHost     = s_http.host;    reqInfo.hostLen   = strlen( s_http.host );
        reqInfo.reqFlags  = HTTP_REQUEST_KEEP_ALIVE_FLAG;   /* one connection, many ranged GETs */

        memset( &reqHdr, 0, sizeof( reqHdr ) );
        reqHdr.pBuffer    = s_reqBuf;
        reqHdr.bufferLen  = sizeof( s_reqBuf );

        hs = HTTPClient_InitializeRequestHeaders( &reqHdr, &reqInfo );
        if( hs != HTTPSuccess )
        {
            OTA_APP_LOG( "http: init request headers FAIL (%s)", HTTPClient_strerror( hs ) );
            return OtaHttpRequestFailed;   /* logic error, not retryable */
        }

        hs = HTTPClient_AddRangeHeader( &reqHdr, ( int32_t ) rangeStart, ( int32_t ) rangeEnd );
        if( hs != HTTPSuccess )
        {
            OTA_APP_LOG( "http: add Range header FAIL (%s)", HTTPClient_strerror( hs ) );
            return OtaHttpRequestFailed;   /* logic error, not retryable */
        }

        transport.pNetworkContext = &s_http.netCtx;   /* unused by our send/recv (state is in s_http) */
        transport.send            = prvHttpTransportSend;
        transport.recv            = prvHttpTransportRecv;

        memset( &response, 0, sizeof( response ) );
        response.pBuffer   = s_respBuf;
        response.bufferLen = sizeof( s_respBuf );
        /* Without a clock, coreHTTP's recv loop has a 0 ms budget and bails the instant transport.recv
         * returns 0 (no data yet) - so the body record (which arrives just after the headers) is never
         * read (bodyLen=0). Give it a ms clock to wait for the body. */
        response.getTime   = prvHttpGetTimeMs;

        hs = HTTPClient_Send( &transport, &reqHdr, NULL, 0, &response, 0 );
        if( hs != HTTPSuccess )
        {
            OTA_APP_LOG( "http: GET [%u,%u] FAIL (%s) status=%u bodyLen=%u - reconnect+retry %u/%u",
                         ( unsigned ) rangeStart, ( unsigned ) rangeEnd, HTTPClient_strerror( hs ),
                         ( unsigned ) response.statusCode, ( unsigned ) response.bodyLen,
                         attempt + 1u, ( unsigned ) OTA_HTTP_REQ_RETRIES );
            /* S3 dropped the keep-alive. Tear the dead connection down (inUse=0; also stops the dead
             * netCtx being reused, which Hard Faulted) and reconnect at the top of the next attempt -
             * NOT a return, so the agent's 10 s RequestTimer never gets involved. */
            prvHttpCloseConn();
            continue;
        }

        /* A ranged GET should yield 206 Partial Content; 200 (whole file) is also acceptable. */
        if( ( response.statusCode != 206 ) && ( response.statusCode != 200 ) )
        {
            OTA_APP_LOG( "http: GET [%u,%u] -> HTTP %u (expected 206)", ( unsigned ) rangeStart,
                         ( unsigned ) rangeEnd, ( unsigned ) response.statusCode );
            return OtaHttpRequestFailed;   /* a real HTTP error (403 expiry, etc.) - not retryable here */
        }

        if( ( response.pBody == NULL ) || ( response.bodyLen == 0 ) )
        {
            OTA_APP_LOG( "http: GET [%u,%u] -> empty body - reconnect+retry %u/%u",
                         ( unsigned ) rangeStart, ( unsigned ) rangeEnd,
                         attempt + 1u, ( unsigned ) OTA_HTTP_REQ_RETRIES );
            /* 206 headers but no body = the keep-alive was half-closed mid-response. Reconnect+retry. */
            prvHttpCloseConn();
            continue;
        }

        /* --- success: body in hand, break out of the retry loop to feed it --- */
        break;
    }

    if( attempt >= OTA_HTTP_REQ_RETRIES )
    {
        OTA_APP_LOG( "http: GET [%u,%u] exhausted %u reconnect attempts - deferring to agent timer",
                     ( unsigned ) rangeStart, ( unsigned ) rangeEnd, ( unsigned ) OTA_HTTP_REQ_RETRIES );
        return OtaHttpRequestFailed;
    }

    /* Hand the raw body to the OTA agent as a file block (decodeFileBlock_Http memcpy()s it as-is). */
    pData = prvEventBufferGet();
    if( pData == NULL )
    {
        OTA_APP_LOG( "http: no free event buffer - dropping block [%u,%u]",
                     ( unsigned ) rangeStart, ( unsigned ) rangeEnd );
        return OtaHttpRequestFailed;
    }

    if( response.bodyLen > sizeof( pData->data ) )
    {
        OTA_APP_LOG( "http: body %u > buffer %u - dropping", ( unsigned ) response.bodyLen,
                     ( unsigned ) sizeof( pData->data ) );
        pData->bufferUsed = false;
        return OtaHttpRequestFailed;
    }

    memcpy( pData->data, response.pBody, response.bodyLen );
    pData->dataLength = ( uint32_t ) response.bodyLen;

    eventMsg.pEventData = pData;
    eventMsg.eventId    = OtaAgentEventReceivedFileBlock;

    if( OTA_SignalEvent( &eventMsg ) == false )
    {
        pData->bufferUsed = false;
        OTA_APP_LOG( "http: OTA_SignalEvent FAIL - buffer released" );
        return OtaHttpRequestFailed;
    }

    return OtaHttpSuccess;
}

/* OtaHttpInterface_t.deinit: close the connection + reset state. */
static OtaHttpStatus_t prvOtaHttpDeinit( void )
{
    if( s_http.isHttps )
    {
        if( s_http.netCtx.pParams != NULL )
        {
            TLS_FreeRTOS_Disconnect( &s_http.netCtx );
        }
    }
    else if( s_http.sock != NULL )
    {
        FreeRTOS_shutdown( s_http.sock, FREERTOS_SHUT_RDWR );
        FreeRTOS_closesocket( s_http.sock );
    }

    memset( &s_http, 0, sizeof( s_http ) );
    return OtaHttpSuccess;
}
#endif /* OTA_APP_ENABLE_HTTP */

/* ---------------------------------------------- MQTT-Agent command helpers ---- */
/* A blocking completion handshake: each wrapper enqueues an MQTTAgent_* command with a
 * completion callback that notifies the calling task and stashes the return code. */
typedef struct
{
    TaskHandle_t  task;
    MQTTStatus_t  returnCode;
} OtaMqttCmdCtx_t;

static void prvCmdCompleteCb( MQTTAgentCommandContext_t * pCmdCtx,
                              MQTTAgentReturnInfo_t * pReturnInfo )
{
    OtaMqttCmdCtx_t * pCtx = ( OtaMqttCmdCtx_t * ) pCmdCtx;

    if( pCtx != NULL )
    {
        pCtx->returnCode = pReturnInfo->returnCode;
        if( pCtx->task != NULL )
        {
            ( void ) xTaskNotifyGive( pCtx->task );
        }
    }
}

/* Block (up to timeoutMs) for the command-complete notification. */
#define OTA_AGENT_CMD_TIMEOUT_MS    ( 10000U )

static bool prvWaitForCmd( OtaMqttCmdCtx_t * pCtx )
{
    ( void ) pCtx;
    /* Wait WITHOUT a timeout. The caller's MQTTAgentSubscribeArgs_t / MQTTSubscribeInfo_t / OtaMqttCmdCtx_t
     * are stack locals that the queued agent command holds by POINTER, so they MUST stay alive until the
     * command resolves. A premature timeout let the caller return (reusing that stack) while the command was
     * still queued, so the agent later dereferenced a stale pTopicFilter (=0x01010102) -> strncmp bus fault
     * in validateSubscribeUnsubscribeParams - hit on the busy OTA_Resume reconnect where the subscribe can't
     * complete within 10s. This never hangs: the agent either runs the command (completion callback fires) or,
     * on disconnect, MQTTAgent_CancelAll fires the callback - both do prvCmdCompleteCb -> xTaskNotifyGive. */
    return ( ulTaskNotifyTake( pdTRUE, portMAX_DELAY ) != 0 ) ? true : false;
}

/* --------------------------------------------- incoming-publish dispatcher ---- */
/* Registered with the subscription_manager for the OTA topic filters. Runs in the
 * agent task's incoming-publish callback. Routes the payload into the OTA event queue. */
static void prvOtaIncomingPublish( void * pvCtx, MQTTPublishInfo_t * pxPublish )
{
    OtaEventMsg_t eventMsg = { 0 };
    OtaEventData_t * pData;
    bool isStream;

    ( void ) pvCtx;

    if( ( pxPublish == NULL ) || ( pxPublish->payloadLength == 0U ) )
    {
        return;
    }

    if( pxPublish->payloadLength > sizeof( pData->data ) )
    {
        OTA_APP_LOG( "RX publish too large (%u > %u) - dropped",
                     ( unsigned ) pxPublish->payloadLength, ( unsigned ) sizeof( pData->data ) );
        return;
    }

    pData = prvEventBufferGet();
    if( pData == NULL )
    {
        OTA_APP_LOG( "RX publish - no free event buffer, dropped" );
        return;
    }

    memcpy( pData->data, pxPublish->pPayload, pxPublish->payloadLength );
    pData->dataLength = ( uint32_t ) pxPublish->payloadLength;

    /* AWS OTA data-plane (file blocks) arrive on "$aws/things/<thing>/streams/...";
     * everything else routed here is a job document. */
    isStream = ( pxPublish->topicNameLength >= 9U ) &&
               ( strstr( pxPublish->pTopicName, "/streams/" ) != NULL );

    eventMsg.pEventData = pData;
    eventMsg.eventId = isStream ? OtaAgentEventReceivedFileBlock
                                : OtaAgentEventReceivedJobDocument;

    if( OTA_SignalEvent( &eventMsg ) == false )
    {
        /* Could not enqueue: release the buffer so it is not leaked. */
        pData->bufferUsed = false;
        OTA_APP_LOG( "RX publish - OTA_SignalEvent FAIL, buffer released" );
    }
}

/* ------------------------------------------------ OTA MQTT interface impl ----- */
static OtaMqttStatus_t prvOtaMqttSubscribe( const char * pTopicFilter,
                                            uint16_t topicFilterLength,
                                            uint8_t ucQoS )
{
    MQTTStatus_t st;
    MQTTAgentSubscribeArgs_t subscribeArgs;
    MQTTAgentCommandInfo_t commandInfo = { 0 };
    OtaMqttCmdCtx_t ctx;

    if( s_pAgent == NULL )
    {
        return OtaMqttSubscribeFailed;
    }

    /* Register the routing callback BEFORE the SUBACK so no early publish is missed. */
    if( SubscriptionManager_AddSubscription( pTopicFilter, topicFilterLength,
                                             prvOtaIncomingPublish, NULL ) == false )
    {
        OTA_APP_LOG( "subscribe: subscription table full for '%.*s'",
                     ( int ) topicFilterLength, pTopicFilter );
        return OtaMqttSubscribeFailed;
    }

    /* The OTA library only SUBSCRIBES to '.../jobs/notify-next' and relies on AWS delivering the
     * $next/get job-document response on '.../jobs/$next/get/accepted' to the requester. HW-caught:
     * that reply-to-requester delivery can NOT be relied on (the broker only delivered it while a
     * matching subscription existed; without one the agent stalls forever at WaitingForJob while
     * telemetry keeps flowing). So when the library subscribes to notify-next, ALSO really
     * SUBSCRIBE to $next/get/accepted on the broker (piggy-backed on the same SUBSCRIBE packet)
     * and register the OTA handler for it. */
    static char s_getAcceptedTopic[ 96 ];
    int getAcceptedLen = 0;
    {
        const char * pNN = "notify-next";
        size_t nnLen = strlen( pNN );
        if( ( topicFilterLength > nnLen ) &&
            ( strncmp( &pTopicFilter[ topicFilterLength - nnLen ], pNN, nnLen ) == 0 ) )
        {
            uint16_t prefixLen = ( uint16_t ) ( topicFilterLength - nnLen );  /* '$aws/things/<t>/jobs/' */
            int n = snprintf( s_getAcceptedTopic, sizeof( s_getAcceptedTopic ),
                              "%.*s$next/get/accepted", ( int ) prefixLen, pTopicFilter );
            if( ( n > 0 ) && ( n < ( int ) sizeof( s_getAcceptedTopic ) ) )
            {
                if( SubscriptionManager_AddSubscription( s_getAcceptedTopic, ( uint16_t ) n,
                                                         prvOtaIncomingPublish, NULL ) )
                {
                    getAcceptedLen = n;
                }
            }
        }
    }

    MQTTSubscribeInfo_t subscribeInfos[ 2 ];
    memset( subscribeInfos, 0, sizeof( subscribeInfos ) );
    subscribeInfos[ 0 ].qos = ( MQTTQoS_t ) ucQoS;
    subscribeInfos[ 0 ].pTopicFilter = pTopicFilter;
    subscribeInfos[ 0 ].topicFilterLength = topicFilterLength;
    if( getAcceptedLen > 0 )
    {
        subscribeInfos[ 1 ].qos = MQTTQoS1;
        subscribeInfos[ 1 ].pTopicFilter = s_getAcceptedTopic;
        subscribeInfos[ 1 ].topicFilterLength = ( uint16_t ) getAcceptedLen;
    }
    subscribeArgs.pSubscribeInfo = subscribeInfos;
    subscribeArgs.numSubscriptions = ( getAcceptedLen > 0 ) ? 2U : 1U;
    subscribeArgs.pProperties = NULL;

    ctx.task = xTaskGetCurrentTaskHandle();
    ctx.returnCode = MQTTSendFailed;
    commandInfo.cmdCompleteCallback = prvCmdCompleteCb;
    commandInfo.pCmdCompleteCallbackContext = ( MQTTAgentCommandContext_t * ) &ctx;
    commandInfo.blockTimeMs = OTA_AGENT_CMD_TIMEOUT_MS;

    st = MQTTAgent_Subscribe( s_pAgent, &subscribeArgs, &commandInfo );
    if( ( st != MQTTSuccess ) || ( prvWaitForCmd( &ctx ) == false ) || ( ctx.returnCode != MQTTSuccess ) )
    {
        OTA_APP_LOG( "subscribe '%.*s' FAIL (enqueue=%d ret=%d)",
                     ( int ) topicFilterLength, pTopicFilter, ( int ) st, ( int ) ctx.returnCode );
        SubscriptionManager_RemoveSubscription( pTopicFilter, topicFilterLength );
        return OtaMqttSubscribeFailed;
    }

    OTA_APP_LOG( "subscribed '%.*s' (qos %u)", ( int ) topicFilterLength, pTopicFilter, ucQoS );
    return OtaMqttSuccess;
}

static OtaMqttStatus_t prvOtaMqttUnsubscribe( const char * pTopicFilter,
                                              uint16_t topicFilterLength,
                                              uint8_t ucQoS )
{
    MQTTStatus_t st;
    MQTTAgentSubscribeArgs_t subscribeArgs;
    MQTTSubscribeInfo_t subscribeInfo;
    MQTTAgentCommandInfo_t commandInfo = { 0 };
    OtaMqttCmdCtx_t ctx;

    if( s_pAgent == NULL )
    {
        return OtaMqttUnsubscribeFailed;
    }

    memset( &subscribeInfo, 0, sizeof( subscribeInfo ) );
    subscribeInfo.qos = ( MQTTQoS_t ) ucQoS;
    subscribeInfo.pTopicFilter = pTopicFilter;
    subscribeInfo.topicFilterLength = topicFilterLength;
    subscribeArgs.pSubscribeInfo = &subscribeInfo;
    subscribeArgs.numSubscriptions = 1U;
    subscribeArgs.pProperties = NULL;

    ctx.task = xTaskGetCurrentTaskHandle();
    ctx.returnCode = MQTTSendFailed;
    commandInfo.cmdCompleteCallback = prvCmdCompleteCb;
    commandInfo.pCmdCompleteCallbackContext = ( MQTTAgentCommandContext_t * ) &ctx;
    commandInfo.blockTimeMs = OTA_AGENT_CMD_TIMEOUT_MS;

    st = MQTTAgent_Unsubscribe( s_pAgent, &subscribeArgs, &commandInfo );
    if( ( st != MQTTSuccess ) || ( prvWaitForCmd( &ctx ) == false ) || ( ctx.returnCode != MQTTSuccess ) )
    {
        OTA_APP_LOG( "unsubscribe '%.*s' FAIL (enqueue=%d ret=%d)",
                     ( int ) topicFilterLength, pTopicFilter, ( int ) st, ( int ) ctx.returnCode );
        /* Remove the routing entry regardless so we stop dispatching. */
        SubscriptionManager_RemoveSubscription( pTopicFilter, topicFilterLength );
        return OtaMqttUnsubscribeFailed;
    }

    SubscriptionManager_RemoveSubscription( pTopicFilter, topicFilterLength );
    OTA_APP_LOG( "unsubscribed '%.*s'", ( int ) topicFilterLength, pTopicFilter );
    return OtaMqttSuccess;
}

static OtaMqttStatus_t prvOtaMqttPublish( const char * const pacTopic,
                                          uint16_t usTopicLen,
                                          const char * pcMsg,
                                          uint32_t ulMsgSize,
                                          uint8_t ucQoS )
{
    MQTTStatus_t st;
    MQTTPublishInfo_t publishInfo;
    MQTTAgentPublishArgs_t publishArgs;
    MQTTAgentCommandInfo_t commandInfo = { 0 };
    OtaMqttCmdCtx_t ctx;

    if( s_pAgent == NULL )
    {
        return OtaMqttPublishFailed;
    }

    memset( &publishInfo, 0, sizeof( publishInfo ) );
    publishInfo.qos = ( MQTTQoS_t ) ucQoS;
    publishInfo.pTopicName = pacTopic;
    publishInfo.topicNameLength = usTopicLen;
    publishInfo.pPayload = pcMsg;
    publishInfo.payloadLength = ulMsgSize;

    publishArgs.pPublishInfo = &publishInfo;
    publishArgs.pProperties = NULL;

    ctx.task = xTaskGetCurrentTaskHandle();
    ctx.returnCode = MQTTSendFailed;
    commandInfo.cmdCompleteCallback = prvCmdCompleteCb;
    commandInfo.pCmdCompleteCallbackContext = ( MQTTAgentCommandContext_t * ) &ctx;
    commandInfo.blockTimeMs = OTA_AGENT_CMD_TIMEOUT_MS;

    st = MQTTAgent_Publish( s_pAgent, &publishArgs, &commandInfo );
    if( ( st != MQTTSuccess ) || ( prvWaitForCmd( &ctx ) == false ) || ( ctx.returnCode != MQTTSuccess ) )
    {
        OTA_APP_LOG( "publish '%.*s' FAIL (enqueue=%d ret=%d)",
                     ( int ) usTopicLen, pacTopic, ( int ) st, ( int ) ctx.returnCode );
        return OtaMqttPublishFailed;
    }

    return OtaMqttSuccess;
}

/* ----------------------------------------------------- OTA app callback ------- */
static void prvOtaAppCallback( OtaJobEvent_t eEvent, const void * pData )
{
    OtaErr_t err;

    switch( eEvent )
    {
        case OtaJobEventActivate:
            OTA_APP_LOG( "job: image ready -> activate (reboot)" );
            err = OTA_ActivateNewImage();   /* should not return (reboots) */
            OTA_APP_LOG( "OTA_ActivateNewImage returned %d (%s)", ( int ) err, OTA_Err_strerror( err ) );
            break;

        case OtaJobEventFail:
            OTA_APP_LOG( "job: FAILED" );
            break;

        case OtaJobEventStartTest:
            /* SIMPLIFICATION: no real self-test - accept immediately. */
            OTA_APP_LOG( "job: self-test -> accepting image (no self-test implemented)" );
            err = OTA_SetImageState( OtaImageStateAccepted );
            if( err != OtaErrNone )
            {
                OTA_APP_LOG( "OTA_SetImageState(Accepted) FAIL %d (%s)", ( int ) err, OTA_Err_strerror( err ) );
            }
            break;

        case OtaJobEventProcessed:
            /* The OtaEventData buffer for a processed event can be recycled. */
            if( pData != NULL )
            {
                ( ( OtaEventData_t * ) pData )->bufferUsed = false;
            }
            break;

        case OtaJobEventUpdateComplete:
            OTA_APP_LOG( "job: update complete" );
            break;

        default:
            OTA_APP_LOG( "job: event %d", ( int ) eEvent );
            break;
    }
}

/* ------------------------------------------------------------ public API ------ */
int ota_app_init( MQTTAgentContext_t * pAgent, const char * pThingName )
{
    OtaErr_t err;

    if( s_initialised )
    {
        /* Already up (e.g. a reconnect re-invoked the connect hook): just refresh the
         * agent pointer and let ota_app_on_mqtt_connected() handle the resume. */
        s_pAgent = pAgent;
        return 0;
    }

    if( ( pAgent == NULL ) || ( pThingName == NULL ) )
    {
        return -1;
    }

    s_pAgent = pAgent;

    s_bufferMutex = xSemaphoreCreateMutexStatic( &s_bufferMutexStorage );
    if( s_bufferMutex == NULL )
    {
        return -1;
    }
#ifdef AWS_STATIC_TASKS
    /* Allocate the decode + event buffers now (post-handshake): keeping them off .bss let the earlier
     * TLS handshake use that heap. ota_app_init runs right after the first MQTT connect, when the heap
     * has settled (steady MQTT uses only a couple of network buffers), so these allocations succeed. */
    s_decodeMem    = ( uint8_t * ) pvPortMalloc( OTA_FILE_BLOCK_SIZE );
    s_eventBuffers = ( OtaEventData_t * ) pvPortMalloc( sizeof( OtaEventData_t ) * OTA_APP_NUM_EVENT_BUFFERS );
    if( ( s_decodeMem == NULL ) || ( s_eventBuffers == NULL ) )
    {
        OTA_APP_LOG( "OTA buffer malloc FAIL (decode=%p events=%p)", ( void * ) s_decodeMem, ( void * ) s_eventBuffers );
        return -1;
    }
#endif
    memset( s_eventBuffers, 0, sizeof( OtaEventData_t ) * OTA_APP_NUM_EVENT_BUFFERS );

    /* OTA application buffers. */
    memset( &s_otaBuffer, 0, sizeof( s_otaBuffer ) );
    s_otaBuffer.pUpdateFilePath    = s_updateFilePath;
    s_otaBuffer.updateFilePathsize = sizeof( s_updateFilePath );
    s_otaBuffer.pCertFilePath      = s_certFilePath;
    s_otaBuffer.certFilePathSize   = sizeof( s_certFilePath );
    s_otaBuffer.pStreamName        = s_streamName;
    s_otaBuffer.streamNameSize     = sizeof( s_streamName );
    s_otaBuffer.pDecodeMemory      = s_decodeMem;
    s_otaBuffer.decodeMemorySize   = OTA_FILE_BLOCK_SIZE;
    s_otaBuffer.pFileBitmap        = s_fileBitmap;
    s_otaBuffer.fileBitmapSize     = sizeof( s_fileBitmap );
#if OTA_APP_ENABLE_HTTP
    /* HTTP data plane: the OTA library copies the job's presigned URL + auth scheme here. */
    s_otaBuffer.pUrl               = ( uint8_t * ) s_otaUrl;
    s_otaBuffer.urlSize            = sizeof( s_otaUrl );
    s_otaBuffer.pAuthScheme        = ( uint8_t * ) s_otaAuthScheme;
    s_otaBuffer.authSchemeSize     = sizeof( s_otaAuthScheme );
#endif

    /* OTA interfaces. */
    memset( &s_otaInterfaces, 0, sizeof( s_otaInterfaces ) );

    s_otaInterfaces.os.event.init   = OtaInitEvent_FreeRTOS;
    s_otaInterfaces.os.event.send   = OtaSendEvent_FreeRTOS;
    s_otaInterfaces.os.event.recv   = OtaReceiveEvent_FreeRTOS;
    s_otaInterfaces.os.event.deinit = OtaDeinitEvent_FreeRTOS;
    s_otaInterfaces.os.timer.start  = OtaStartTimer_FreeRTOS;
    s_otaInterfaces.os.timer.stop   = OtaStopTimer_FreeRTOS;
    s_otaInterfaces.os.timer.delete = OtaDeleteTimer_FreeRTOS;
    s_otaInterfaces.os.mem.malloc   = Malloc_FreeRTOS;
    s_otaInterfaces.os.mem.free     = Free_FreeRTOS;

    s_otaInterfaces.mqtt.subscribe   = prvOtaMqttSubscribe;
    s_otaInterfaces.mqtt.unsubscribe = prvOtaMqttUnsubscribe;
    s_otaInterfaces.mqtt.publish     = prvOtaMqttPublish;

#if OTA_APP_ENABLE_HTTP
    /* HTTP data interface: download file blocks over HTTPS from the presigned S3 URL. The job's
     * "protocols":["HTTP"] + this non-NULL interface make setDataInterface pick HTTP (ota_interface.c). */
    s_otaInterfaces.http.init    = prvOtaHttpInit;
    s_otaInterfaces.http.request = prvOtaHttpRequest;
    s_otaInterfaces.http.deinit  = prvOtaHttpDeinit;
#else
    /* MQTT-only data plane (BL602): leave http.* NULL so setDataInterface picks MQTT/CBOR. The
     * OTA job must advertise "protocols":["MQTT"] (or the agent rejects an HTTP-only job doc). */
    s_otaInterfaces.http.init    = NULL;
    s_otaInterfaces.http.request = NULL;
    s_otaInterfaces.http.deinit  = NULL;
#endif

    s_otaInterfaces.pal = *ota_pal_get_interface();

    err = OTA_Init( &s_otaBuffer, &s_otaInterfaces,
                    ( const uint8_t * ) pThingName, prvOtaAppCallback );
    if( err != OtaErrNone )
    {
        OTA_APP_LOG( "OTA_Init FAIL %d (%s)", ( int ) err, OTA_Err_strerror( err ) );
        return -1;
    }

    /* Dedicated OTA agent task: OTA_EventProcessingTask runs the state machine. Kept DYNAMIC (not a
     * static .bss stack) on purpose: it is created only AFTER the first MQTT connect, so a dynamic
     * stack is NOT reserved during the earlier TLS handshake - freeing that ~10 KB for the handshake's
     * working set, which is the bl602 heap bottleneck. The agent task (aws_iot_demo.c) stays static so
     * it is guaranteed to create; this one retries on the next reconnect if a fragmented heap fails it. */
    if( xTaskCreate( OTA_EventProcessingTask, "ota_agent",
                     otaconfigSTACK_SIZE, NULL,
                     otaconfigAGENT_PRIORITY, NULL ) != pdPASS )
    {
        OTA_APP_LOG( "xTaskCreate(ota_agent) FAIL" );
        return -1;
    }

    s_initialised = 1;
    OTA_APP_LOG( "agent initialised (thing '%s')", pThingName );
    return 0;
}

void ota_app_on_mqtt_connected( void )
{
    OtaEventMsg_t eventMsg = { 0 };
    OtaState_t state;

    if( !s_initialised )
    {
        return;
    }

    state = OTA_GetState();

    if( state == OtaAgentStateSuspended )
    {
        OTA_APP_LOG( "MQTT reconnected -> OTA_Resume" );
        ( void ) OTA_Resume();
    }
    else if( state == OtaAgentStateStopped )
    {
        OTA_APP_LOG( "MQTT connected -> OtaAgentEventStart" );
        eventMsg.eventId = OtaAgentEventStart;
        ( void ) OTA_SignalEvent( &eventMsg );
    }
    else
    {
        /* Init / Ready: kick the state machine to request a job. */
        OTA_APP_LOG( "MQTT connected -> OtaAgentEventStart (state %d)", ( int ) state );
        eventMsg.eventId = OtaAgentEventStart;
        ( void ) OTA_SignalEvent( &eventMsg );
    }
}

void ota_app_on_mqtt_disconnected( void )
{
    OtaState_t state;

    if( !s_initialised )
    {
        return;
    }

    /* Suspend the OTA agent while the MQTT link is down. Without this, on reconnect the agent is
     * still in a running state (e.g. WaitingForJob) so ota_app_on_mqtt_connected() hits the else
     * branch and re-sends OtaAgentEventStart - invalid in that state ("Received unexpected event:
     * ... Start"), and worse the job/stream subscriptions are silently gone (cleanSession=true)
     * so the OTA goes deaf. Suspending here means the reconnect lands in OtaAgentStateSuspended ->
     * OTA_Resume(), which re-subscribes the job topics. The reconnect's TLS handshake gives the
     * OTA task ample time to process the suspend before the next connect. */
    state = OTA_GetState();
    if( ( state != OtaAgentStateStopped ) &&
        ( state != OtaAgentStateSuspended ) &&
        ( state != OtaAgentStateInit ) )
    {
        OTA_APP_LOG( "MQTT disconnected -> OTA_Suspend (state %d)", ( int ) state );
        ( void ) OTA_Suspend();
    }
}
