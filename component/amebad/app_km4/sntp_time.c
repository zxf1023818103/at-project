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
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_DNS.h"     /* FreeRTOS_gethostbyname */

#include "core_sntp_client.h"
#include "core_sntp_serializer.h"
#include "syscalls.h"         /* amebad_set_unix_time */

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
#define SNTP_RETRY_MS               ( 10000U )  /* backoff after a failed round */

static SntpServerInfo_t s_servers[] =
{
    { "time.google.com",     sizeof( "time.google.com" ) - 1,     SNTP_DEFAULT_SERVER_PORT },
    { "pool.ntp.org",        sizeof( "pool.ntp.org" ) - 1,        SNTP_DEFAULT_SERVER_PORT },
    { "time.cloudflare.com", sizeof( "time.cloudflare.com" ) - 1, SNTP_DEFAULT_SERVER_PORT },
};
static SntpServerInfo_t s_env_server[ 1 ];
static char             s_env_server_name[ 64 ];
static uint8_t          s_netbuf[ SNTP_PACKET_BASE_SIZE ];
static SntpContext_t    s_ctx;
static NetworkContext_t s_netctx;
static TaskHandle_t     s_selftest_task;
static volatile int     s_first_sync_done;

/* ---- coreSNTP callbacks --------------------------------------------------------------- */

static bool sntp_resolve_dns( const SntpServerInfo_t * srv, uint32_t * ip )
{
    *ip = FreeRTOS_gethostbyname( srv->pServerName );   /* network-order IPv4, 0 on failure */
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
        printf( "[SNTP] convert-to-unix failed\n\r" );
        return;
    }

    amebad_set_unix_time( ( time_t ) secs, usec );

    /* Mirror UTC into the hardware RTC so a warm reset starts on the right time
     * (rtc_time.c seeds the wall clock from it at boot, before SNTP re-syncs). */
    {
        extern void rtc_time_store( time_t secs );
        rtc_time_store( ( time_t ) secs );
    }

    printf( "[SNTP] sync ok: unix=%lu off=%lds via %s\n\r",
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

/* ---- tasks ----------------------------------------------------------------------------- */

static void sntp_task( void * arg )
{
    UdpTransportInterface_t udp;
    const SntpServerInfo_t * servers = s_servers;
    size_t num_servers = sizeof( s_servers ) / sizeof( s_servers[ 0 ] );
    const char * env_srv;
    TickType_t rcv_to = pdMS_TO_TICKS( SNTP_SOCK_RCVTIMEO_MS );
    struct freertos_sockaddr bind_addr;
    SntpStatus_t st;

    ( void ) arg;
    srand( ( unsigned ) xTaskGetTickCount() );

    /* NTP server config lives in the NTP_SERVER env var; only when it is unset/empty, default it
     * to a China-reachable server (the public pool / time.google / time.cloudflare time out on
     * some networks here). A value set/persisted via `set NTP_SERVER <host>` is kept. Mirrors
     * load_ntp_server() in component/example/qwen/ntp.c. */
    env_srv = getenv( "NTP_SERVER" );
    if( ( env_srv == NULL ) || ( env_srv[ 0 ] == '\0' ) )
    {
        setenv( "NTP_SERVER", "ntp.aliyun.com", 1 );
        env_srv = getenv( "NTP_SERVER" );
    }
    if( ( env_srv != NULL ) && ( env_srv[ 0 ] != '\0' ) )
    {
        strncpy( s_env_server_name, env_srv, sizeof( s_env_server_name ) - 1 );
        s_env_server[ 0 ].pServerName = s_env_server_name;
        s_env_server[ 0 ].serverNameLen = strlen( s_env_server_name );
        s_env_server[ 0 ].port = SNTP_DEFAULT_SERVER_PORT;
        servers = s_env_server;
        num_servers = 1;
        printf( "[SNTP] using NTP_SERVER=%s\n\r", s_env_server_name );
    }

    /* One persistent, ephemeral-bound UDP socket for the task lifetime. */
    s_netctx.sock = FreeRTOS_socket( FREERTOS_AF_INET, FREERTOS_SOCK_DGRAM, FREERTOS_IPPROTO_UDP );
    if( s_netctx.sock == FREERTOS_INVALID_SOCKET )
    {
        printf( "[SNTP] socket FAILED\n\r" );
        vTaskDelete( NULL );
        return;
    }
    memset( &bind_addr, 0, sizeof( bind_addr ) );
    bind_addr.sin_family = FREERTOS_AF_INET;
    bind_addr.sin_port = 0;
    ( void ) FreeRTOS_bind( s_netctx.sock, &bind_addr, sizeof( bind_addr ) );
    ( void ) FreeRTOS_setsockopt( s_netctx.sock, 0, FREERTOS_SO_RCVTIMEO, &rcv_to, sizeof( rcv_to ) );

    udp.pUserContext = &s_netctx;
    udp.sendTo = sntp_udp_send;
    udp.recvFrom = sntp_udp_recv;

    st = Sntp_Init( &s_ctx, servers, num_servers, SNTP_RESPONSE_TIMEOUT_MS,
                    s_netbuf, sizeof( s_netbuf ),
                    sntp_resolve_dns, sntp_get_time, sntp_set_time, &udp, NULL );
    if( st != SntpSuccess )
    {
        printf( "[SNTP] Sntp_Init failed (%d)\n\r", ( int ) st );
        FreeRTOS_closesocket( s_netctx.sock );
        vTaskDelete( NULL );
        return;
    }

    printf( "[SNTP] client up; %u server(s)\n\r", ( unsigned ) num_servers );

    for( ; ; )
    {
        st = Sntp_SendTimeRequest( &s_ctx, ( uint32_t ) rand(), SNTP_SEND_BLOCK_MS );
        if( st == SntpSuccess )
        {
            st = Sntp_ReceiveTimeResponse( &s_ctx, SNTP_RECV_BLOCK_MS );
        }

        if( st == SntpSuccess )
        {
            vTaskDelay( pdMS_TO_TICKS( SNTP_RESYNC_PERIOD_MS ) );   /* synced; re-sync in ~1h */
        }
        else
        {
            printf( "[SNTP] round failed (%d); retry in %us\n\r", ( int ) st, SNTP_RETRY_MS / 1000U );
            vTaskDelay( pdMS_TO_TICKS( SNTP_RETRY_MS ) );
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
    /* Create the self-test waiter first so its handle is set before any sync notify. */
    if( xTaskCreate( sntp_selftest_task, "time_test", 2048, NULL,
                     tskIDLE_PRIORITY + 1, &s_selftest_task ) != pdPASS )
    {
        printf( "[SNTP] xTaskCreate(time_test) FAILED\n\r" );
    }
    if( xTaskCreate( sntp_task, "sntp", 4096, NULL,
                     tskIDLE_PRIORITY + 2, NULL ) != pdPASS )
    {
        printf( "[SNTP] xTaskCreate(sntp) FAILED\n\r" );
    }
}
