/*
 * wifi_service.c — the shared WiFi provisioning + reconnect policy (see wifi_service.h).
 * Platform-agnostic: it only touches FreeRTOS, the POSIX env (KV-backed), and the wifi_port
 * HAL. One task connects and then supervises the link, re-associating on every drop.
 */
#include "wifi_service.h"
#include "wifi_port.h"

#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"

#include "backoff_algorithm.h"   /* exponential-backoff-with-jitter for the reconnect retries */

#include <stdio.h>
#include <stdlib.h>   /* getenv / setenv / unsetenv / atoi */
#include <string.h>

/* syscall_config: persist one env key to /KV (getenv()==NULL -> erase KV/<name>). */
extern int env_save( const char * name );

#define WSVC_EVT_DISCONNECT   ( 1u << 0 )   /* set by wifi_service_on_event on a link drop */

/* Reconnect retry policy: exponential backoff (BackoffAlgorithm, the same library the AWS agent uses)
 * with full jitter, 1 s base doubling to an 8 s cap, retrying forever until the AP is back. */
#define WSVC_BACKOFF_BASE_MS   1000u
#define WSVC_BACKOFF_MAX_MS    8000u

/* Small self-contained PRNG for the backoff jitter (xorshift, seeded from the tick on first use) so the
 * global rand() state is left untouched and two devices don't retry in lock-step. */
static uint32_t wsvc_rand( void )
{
    static uint32_t s;

    if( s == 0u )
    {
        s = ( uint32_t ) xTaskGetTickCount() | 1u;
    }
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

static EventGroupHandle_t s_evt;
static TaskHandle_t       s_task;
static volatile int       s_forget;         /* `wifi forget` issued -> stay down until re-enabled */

/* Try to associate: cached WIFI_CHANNEL for an instant re-connect, else a directed scan.
 * On a successful scan, cache the channel (+ security if the platform reports one) so the
 * next attempt skips the scan. Returns 0 when associated. */
static int wsvc_connect_once( const char * ssid, const char * pass )
{
    const char * ce = getenv( "WIFI_CHANNEL" );
    int          cached = ( ce != NULL && ce[ 0 ] != '\0' ) ? atoi( ce ) : 0;

    if( cached >= 1 && cached <= 196 )
    {
        int sec = wifi_port_sec_from_str( getenv( "WIFI_SECURITY" ) );
        printf( "[WSVC] %s cached ch=%d -> direct connect (no scan)\r\n", ssid, cached );
        if( wifi_port_connect( ssid, pass, ( uint8_t ) cached, sec ) == 0 )
        {
            return 0;
        }
        printf( "[WSVC] cached ch=%d failed -> clear cache + scan\r\n", cached );
        unsetenv( "WIFI_CHANNEL" );
        env_save( "WIFI_CHANNEL" );
    }

    {
        uint8_t chan = 0;
        int     sec = -1;
        if( wifi_port_scan( ssid, &chan, &sec ) != 0 )
        {
            printf( "[WSVC] %s not found in scan\r\n", ssid );
            return -1;
        }
        printf( "[WSVC] %s found ch=%u sec=%d -> connect\r\n", ssid, ( unsigned ) chan, sec );
        {
            char cbuf[ 8 ];
            ( void ) snprintf( cbuf, sizeof( cbuf ), "%u", ( unsigned ) chan );
            if( setenv( "WIFI_CHANNEL", cbuf, 1 ) == 0 ) { env_save( "WIFI_CHANNEL" ); }
            if( sec >= 0 )
            {
                const char * ss = wifi_port_sec_to_str( sec );
                if( ss != NULL && setenv( "WIFI_SECURITY", ss, 1 ) == 0 ) { env_save( "WIFI_SECURITY" ); }
            }
        }
        return wifi_port_connect( ssid, pass, chan, sec );
    }
}

static void wsvc_task( void * arg )
{
    BackoffAlgorithmContext_t reconnectParams;
    uint16_t                  nextBackoffMs = 0u;
    int                       warned = 0;

    ( void ) arg;

    ( void ) wifi_port_enable();
    wifi_port_autoreconnect_disable();   /* wifi_service is the single reconnect authority */
    BackoffAlgorithm_InitializeParams( &reconnectParams, WSVC_BACKOFF_BASE_MS, WSVC_BACKOFF_MAX_MS,
                                       BACKOFF_ALGORITHM_RETRY_FOREVER );

    for( ; ; )
    {
        const char * ssid;
        const char * pass;

        if( s_forget )
        {
            /* Parked after a `wifi forget`; woken by wifi_reconnect_enable(). */
            ( void ) ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
            BackoffAlgorithm_InitializeParams( &reconnectParams, WSVC_BACKOFF_BASE_MS,
                                               WSVC_BACKOFF_MAX_MS, BACKOFF_ALGORITHM_RETRY_FOREVER );
            continue;
        }

        /* Credentials from env, re-read each pass so a `setenv`/`wifi forget` takes effect. */
        ssid = getenv( "WIFI_SSID" );
        pass = getenv( "WIFI_PASSWORD" );
        if( ( ssid == NULL ) || ( ssid[ 0 ] == '\0' ) || ( pass == NULL ) )
        {
            if( !warned )
            {
                printf( "[WSVC] no WIFI_SSID/PASSWORD provisioned - staying offline "
                        "(setenv WIFI_SSID/WIFI_PASSWORD)\r\n" );
                warned = 1;
            }
            /* Poll so a later `setenv` (no reboot) is picked up; also woken by notify. */
            ( void ) ulTaskNotifyTake( pdTRUE, pdMS_TO_TICKS( 3000 ) );
            continue;
        }
        warned = 0;

        /* Clear stale disconnect before connecting so a drop during/after the attempt is caught. */
        ( void ) xEventGroupClearBits( s_evt, WSVC_EVT_DISCONNECT );

        if( wsvc_connect_once( ssid, pass ) == 0 )
        {
            /* Connected: reset the backoff and block until the link drops (or a forget sets the bit). */
            BackoffAlgorithm_InitializeParams( &reconnectParams, WSVC_BACKOFF_BASE_MS,
                                               WSVC_BACKOFF_MAX_MS, BACKOFF_ALGORITHM_RETRY_FOREVER );
            ( void ) xEventGroupWaitBits( s_evt, WSVC_EVT_DISCONNECT, pdTRUE, pdFALSE, portMAX_DELAY );
            printf( "[WSVC] link down -> re-associating\r\n" );
        }
        else
        {
            /* Connect failed: exponential-backoff-with-jitter before retrying. RETRY_FOREVER never
             * exhausts, so the return status is ignored (nextBackoffMs is always set). */
            ( void ) BackoffAlgorithm_GetNextBackoff( &reconnectParams, wsvc_rand(), &nextBackoffMs );
            vTaskDelay( pdMS_TO_TICKS( nextBackoffMs ) );
        }
    }
}

void wifi_service_start( void )
{
    if( s_task != NULL )
    {
        return;   /* idempotent */
    }
    if( s_evt == NULL )
    {
        s_evt = xEventGroupCreate();
    }
    ( void ) xTaskCreate( wsvc_task, "wifi_svc", ( configSTACK_DEPTH_TYPE ) wifi_port_task_stack_words(),
                          NULL, tskIDLE_PRIORITY + 2, &s_task );
}

void wifi_service_on_event( wifi_svc_event_t ev )
{
    if( s_evt == NULL )
    {
        return;
    }
    if( ev == WIFI_SVC_EV_DISCONNECT )
    {
        ( void ) xEventGroupSetBits( s_evt, WSVC_EVT_DISCONNECT );
    }
    /* CONNECTED / GOTIP: the connect result is handled synchronously inside wifi_port_connect,
     * and SNTP is kicked from the platform GOTIP glue, so the monitor only needs DISCONNECT. */
}

void wifi_forget( void )
{
    static const char * const keys[] = {
        "WIFI_SSID", "WIFI_PASSWORD", "WIFI_CHANNEL", "WIFI_SECURITY",
    };
    unsigned i;

    s_forget = 1;
    wifi_port_disconnect();
    for( i = 0; i < sizeof( keys ) / sizeof( keys[ 0 ] ); i++ )
    {
        unsetenv( keys[ i ] );
        env_save( keys[ i ] );   /* getenv()==NULL -> erase KV/<name> */
    }
    /* Break the steady-state wait so the loop notices s_forget and parks. */
    if( s_evt != NULL ) { ( void ) xEventGroupSetBits( s_evt, WSVC_EVT_DISCONNECT ); }
    printf( "[WSVC] WiFi forgotten: disconnected, reconnect disabled, credentials erased\r\n" );
}

void wifi_reconnect_enable( void )
{
    s_forget = 0;
    if( s_task != NULL )
    {
        xTaskNotifyGive( s_task );   /* unpark the monitor */
    }
}
