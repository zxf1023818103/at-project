/*
 * sntp_time.c — coreSNTP UTC time-sync client for the AmebaD KM4 (AT_PROJECT_SNTP image).
 *
 * Fetches UTC from public NTP servers over UDP/123 (FreeRTOS-Plus-TCP) and feeds it into
 * newlib's wall clock via amebad_set_unix_time() (time_base.c). Plain SNTPv4, no auth.
 * coreSNTP is transport/clock-agnostic; this file supplies the four interfaces it needs:
 * DNS-resolve, get-time, set-time, and a UDP send/recv transport.
 *
 * On the FIRST successful sync it notifies the one-shot time self-test task (time_selftest.c)
 * so that test sees a real UTC clock rather than 1970+uptime.
 *
 * Server override: the NTP_SERVER env var (`set NTP_SERVER <host>`, persisted in KV/ via the env
 * layer) replaces the built-in list with a single server; it defaults to ntp.aliyun.com when unset.
 */
#include <stdio.h>
#include <stdlib.h>            /* rand, srand, getenv */
#include <string.h>
#include <stdint.h>
#include <sys/time.h>         /* gettimeofday, struct timeval */

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"           /* s_sntp_lock: serialize sntp_sync_once (task vs console) */
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_DNS.h"     /* FreeRTOS_gethostbyname */

#include "core_sntp_client.h"
#include "core_sntp_serializer.h"
#include "backoff_algorithm.h"   /* truncated exp backoff-with-jitter for the retry loop */
#include "syscalls.h"         /* amebad_set_unix_time */
#include "at_log.h"              /* rxi log.c: log_info / log_error */

/* coreSNTP's opaque transport context: our bound UDP socket. */
struct NetworkContext
{
    Socket_t sock;
};

#define SNTP_RESPONSE_TIMEOUT_MS    ( 5000U )   /* per-server response timeout (Sntp_Init) */
#define SNTP_SEND_BLOCK_MS          ( 2000U )   /* Sntp_SendTimeRequest block time */
#define SNTP_RECV_BLOCK_MS          ( 5000U )   /* Sntp_ReceiveTimeResponse block time */
#define SNTP_SOCK_RCVTIMEO_MS       ( 50U )     /* per-recvFrom socket timeout (coreSNTP polls) */
#define SNTP_RESYNC_PERIOD_MS       ( 3600U * 1000U )   /* re-sync every hour */
#define SNTP_BACKOFF_BASE_MS        ( 1000U )   /* truncated binary exponential backoff-with-jitter on a
                                                 * failed round: first retry ~0-1s, the cap doubles each */
#define SNTP_BACKOFF_MAX_MS         ( 32000U )  /* consecutive failure up to this 32 s cap (uint16_t-bounded) */

static SntpServerInfo_t  s_env_server[ 1 ];
static char              s_env_server_name[ 64 ];
static SemaphoreHandle_t s_sntp_lock;          /* serializes sntp_sync_once (task vs console) */

/* NTP server from DHCP option 42 (FreeRTOS_DHCP.c at-project patch); network byte order, 0 = none. */
extern uint32_t ulDHCPNtpServerAddress;
static TaskHandle_t      s_selftest_task;
static volatile int      s_first_sync_done;

/* ---- coreSNTP callbacks --------------------------------------------------------------- */

static bool sntp_resolve_dns( const SntpServerInfo_t * srv, uint32_t * ip )
{
    *ip = FreeRTOS_gethostbyname( srv->pServerName );   /* network-order IPv4, 0 on failure;
                                                         * already handles a dotted-decimal IP (a
                                                         * DHCP-learned NTP server) via inet_addr. */
    return ( *ip != 0U );
}

static void sntp_get_time( SntpTimestamp_t * t )
{
    struct timeval tv;

    gettimeofday( &tv, NULL );      /* -> time_base.c; 1970+uptime before the first sync */
    t->seconds   = ( uint32_t ) tv.tv_sec + SNTP_TIME_AT_UNIX_EPOCH_SECS;
    t->fractions = ( uint32_t ) tv.tv_usec * SNTP_FRACTION_VALUE_PER_MICROSECOND;
}

static void sntp_set_time( const SntpServerInfo_t * srv,
                           const SntpTimestamp_t * st,
                           int64_t offMs,
                           SntpLeapSecondInfo_t leap )
{
    uint32_t secs = 0, usec = 0;

    ( void ) leap;
    if( Sntp_ConvertToUnixTime( st, &secs, &usec ) != SntpSuccess )
    {
        log_error( "[SNTP] convert-to-unix failed" );
        return;
    }

    amebad_set_unix_time( ( time_t ) secs, usec );

    /* Mirror UTC into the hardware RTC so a warm reset starts on the right time
     * (rtc_time.c seeds the wall clock from it at boot, before SNTP re-syncs). */
    {
        extern void rtc_time_store( time_t secs );
        rtc_time_store( ( time_t ) secs );
    }

    log_info( "[SNTP] sync ok: unix=%lu off=%lds via %s",
            ( unsigned long ) secs, ( long ) ( offMs / 1000 ), srv->pServerName );

    if( !s_first_sync_done )
    {
        s_first_sync_done = 1;
        if( s_selftest_task != NULL )
        {
            xTaskNotifyGive( s_selftest_task );
        }
    }
}

/* ---- UDP transport (FreeRTOS-Plus-TCP) ------------------------------------------------- */

static int32_t sntp_udp_send( NetworkContext_t * ctx, uint32_t addr, uint16_t port,
                              const void * buf, uint16_t len )
{
    struct freertos_sockaddr to;

    memset( &to, 0, sizeof( to ) );
    to.sin_family = FREERTOS_AF_INET;
    to.sin_port = FreeRTOS_htons( port );
    to.sin_address.ulIP_IPv4 = addr;
    /* Returns len on success, 0 on a transiently-full TX path (coreSNTP retries). */
    return FreeRTOS_sendto( ctx->sock, buf, len, 0, &to, sizeof( to ) );
}

static int32_t sntp_udp_recv( NetworkContext_t * ctx, uint32_t addr, uint16_t port,
                              void * buf, uint16_t len )
{
    struct freertos_sockaddr from;
    socklen_t fromlen = sizeof( from );
    int32_t r;

    ( void ) addr;
    ( void ) port;
    r = FreeRTOS_recvfrom( ctx->sock, buf, len, 0, &from, &fromlen );
    /* >0 = bytes; <=0 (timeout/EWOULDBLOCK) -> 0 so coreSNTP retries until its block time. */
    return ( r > 0 ) ? r : 0;
}

/* ---- one-shot sync (shared by the background task and the `sntp_update` command) ------- */

/* Resolve the server list from the NTP_SERVER env var, defaulting it to a China-reachable
 * server when unset/empty (the public pool / time.google / time.cloudflare time out on some
 * networks here). Re-read on every sync so a runtime `set NTP_SERVER <host>` takes effect at
 * the next round. Caller holds s_sntp_lock (this writes the shared s_env_server_name). */
static size_t pick_servers( const SntpServerInfo_t ** out )
{
    const char * env_srv = getenv( "NTP_SERVER" );

    /* Priority: explicit NTP_SERVER env > NTP server learned from DHCP option 42 (captured by the
     * FreeRTOS_DHCP.c at-project patch into ulDHCPNtpServerAddress) > built-in default. The default is
     * NOT persisted (no setenv) so a DHCP-learned NTP is still used on a later boot; an explicit
     * `set NTP_SERVER <host>` is honoured and persists through the env layer. */
    if( ( env_srv != NULL ) && ( env_srv[ 0 ] != '\0' ) )
    {
        strncpy( s_env_server_name, env_srv, sizeof( s_env_server_name ) - 1 );
    }
    else if( ulDHCPNtpServerAddress != 0U )
    {
        uint32_t a = ulDHCPNtpServerAddress;
        snprintf( s_env_server_name, sizeof( s_env_server_name ), "%d.%d.%d.%d",
                  (int)( a & 0xff ), (int)( ( a >> 8 ) & 0xff ),
                  (int)( ( a >> 16 ) & 0xff ), (int)( ( a >> 24 ) & 0xff ) );
        log_info( "[SNTP] using DHCP NTP server %s", s_env_server_name );
    }
    else
    {
        /* Neither an explicit NTP_SERVER nor a DHCP-advertised one (option 42): fall back to the
         * fixed default ntp.aliyun.com (reachable on the networks here; the public pool / time.google
         * / time.cloudflare time out). */
        strncpy( s_env_server_name, "ntp.aliyun.com", sizeof( s_env_server_name ) - 1 );
        log_info( "[SNTP] no NTP_SERVER / DHCP NTP - using default ntp.aliyun.com" );
    }
    s_env_server_name[ sizeof( s_env_server_name ) - 1 ] = '\0';
    s_env_server[ 0 ].pServerName   = s_env_server_name;
    s_env_server[ 0 ].serverNameLen = strlen( s_env_server_name );
    s_env_server[ 0 ].port          = SNTP_DEFAULT_SERVER_PORT;
    *out = s_env_server;
    return 1;
}

/* Force one SNTP round NOW: (re)read NTP_SERVER, open an ephemeral UDP socket, send a request,
 * wait for the response, and on success let sntp_set_time() update the wall clock + RTC. Self-
 * contained (local socket / context / buffer) and mutex-serialized, so the background task and
 * the console command can both call it. Returns 0 on success, negative on failure.
 *
 * Strong def: overrides the weak no-op stub in time_cli.c, so `sntp_update` actually syncs in
 * any image that links this file (make SNTP=ON). */
int sntp_sync_once( void )
{
    UdpTransportInterface_t  udp;
    NetworkContext_t         netctx;
    SntpContext_t            ctx;
    uint8_t                  netbuf[ SNTP_PACKET_BASE_SIZE ];
    const SntpServerInfo_t * servers;
    size_t                   num_servers;
    struct freertos_sockaddr bind_addr;
    TickType_t               rcv_to = pdMS_TO_TICKS( SNTP_SOCK_RCVTIMEO_MS );
    SntpStatus_t             st;
    int                      rc = -1;

    if( s_sntp_lock == NULL )
    {
        s_sntp_lock = xSemaphoreCreateMutex();
    }
    if( s_sntp_lock != NULL )
    {
        ( void ) xSemaphoreTake( s_sntp_lock, portMAX_DELAY );
    }

    num_servers = pick_servers( &servers );

    netctx.sock = FreeRTOS_socket( FREERTOS_AF_INET, FREERTOS_SOCK_DGRAM, FREERTOS_IPPROTO_UDP );
    if( netctx.sock == FREERTOS_INVALID_SOCKET )
    {
        log_error( "[SNTP] socket FAILED" );
        goto out;
    }
    memset( &bind_addr, 0, sizeof( bind_addr ) );
    bind_addr.sin_family = FREERTOS_AF_INET;
    bind_addr.sin_port   = 0;
    ( void ) FreeRTOS_bind( netctx.sock, &bind_addr, sizeof( bind_addr ) );
    ( void ) FreeRTOS_setsockopt( netctx.sock, 0, FREERTOS_SO_RCVTIMEO, &rcv_to, sizeof( rcv_to ) );

    udp.pUserContext = &netctx;
    udp.sendTo       = sntp_udp_send;
    udp.recvFrom     = sntp_udp_recv;

    st = Sntp_Init( &ctx, servers, num_servers, SNTP_RESPONSE_TIMEOUT_MS,
                    netbuf, sizeof( netbuf ),
                    sntp_resolve_dns, sntp_get_time, sntp_set_time, &udp, NULL );
    if( st != SntpSuccess )
    {
        log_error( "[SNTP] Sntp_Init failed (%d)", ( int ) st );
        FreeRTOS_closesocket( netctx.sock );
        goto out;
    }

    st = Sntp_SendTimeRequest( &ctx, ( uint32_t ) rand(), SNTP_SEND_BLOCK_MS );
    if( st == SntpSuccess )
    {
        st = Sntp_ReceiveTimeResponse( &ctx, SNTP_RECV_BLOCK_MS );   /* sntp_set_time sets the clock */
    }
    FreeRTOS_closesocket( netctx.sock );
    rc = ( st == SntpSuccess ) ? 0 : -2;

out:
    if( s_sntp_lock != NULL )
    {
        ( void ) xSemaphoreGive( s_sntp_lock );
    }
    return rc;
}

/* ---- tasks ----------------------------------------------------------------------------- */

static void sntp_task( void * arg )
{
    BackoffAlgorithmContext_t backoff;

    ( void ) arg;
    srand( ( unsigned ) xTaskGetTickCount() );   /* jitter source for BackoffAlgorithm_GetNextBackoff */
    BackoffAlgorithm_InitializeParams( &backoff, SNTP_BACKOFF_BASE_MS, SNTP_BACKOFF_MAX_MS,
                                       BACKOFF_ALGORITHM_RETRY_FOREVER );
    log_info( "[SNTP] client up" );

    for( ; ; )
    {
        int rc = sntp_sync_once();

        if( rc == 0 )
        {
            /* synced: reset the backoff so the next outage starts from the base, re-sync in ~1h */
            BackoffAlgorithm_InitializeParams( &backoff, SNTP_BACKOFF_BASE_MS, SNTP_BACKOFF_MAX_MS,
                                               BACKOFF_ALGORITHM_RETRY_FOREVER );
            vTaskDelay( pdMS_TO_TICKS( SNTP_RESYNC_PERIOD_MS ) );
        }
        else
        {
            uint16_t delay_ms = SNTP_BACKOFF_MAX_MS;
            ( void ) BackoffAlgorithm_GetNextBackoff( &backoff, ( uint32_t ) rand(), &delay_ms );
            log_error( "[SNTP] round failed (%d); retry in %ums (exp backoff)", rc, ( unsigned ) delay_ms );
            vTaskDelay( pdMS_TO_TICKS( delay_ms ) );
        }
    }
}

/* One-shot: wait for the first SNTP sync, then run the newlib date/time self-test. */
static void sntp_selftest_task( void * arg )
{
    extern void time_selftest_run( void );

    ( void ) arg;
    ( void ) ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
    time_selftest_run();
    vTaskDelete( NULL );
}

void sntp_sync_start( void )
{
    /* The [TIME]/[DST] newlib date-time self-test demo (time_selftest_run via sntp_selftest_task)
     * is disabled: it repeatedly switches TZ (CST/PST/UTC/...), which makes the per-line log
     * timestamp jump around and clutters the operational log. Re-enable by restoring the
     * xTaskCreate(sntp_selftest_task, ...) call (the notify in sntp_set_time is gated on
     * s_selftest_task, so it stays a no-op while this is off). */
    ( void ) sntp_selftest_task;
    if( xTaskCreate( sntp_task, "sntp", 4096, NULL,
                     tskIDLE_PRIORITY + 2, NULL ) != pdPASS )
    {
        log_error( "[SNTP] xTaskCreate(sntp) FAILED" );
    }
}
