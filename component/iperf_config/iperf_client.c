/*
 * Minimal iperf2 TCP client for the AmebaD FreeRTOS+TCP bring-up — measures
 * device->host (TX / upload) WiFi throughput against an `iperf -s` running on the
 * WSL host. WSL is in mirrored mode and sits on the AIoT subnet at
 * IPERF_SERVER_IP, directly reachable from the device.
 *
 * Host side:   iperf -s -i 1
 * It connects, sends a zeroed iperf2 client header (flags = 0 -> plain client->
 * server test), then streams for IPERF_SECONDS and closes; `iperf -s` reports the
 * received bandwidth.
 */
#include "FreeRTOS.h"
#include "at_log.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"


#define IPERF_SERVER_IP   FreeRTOS_inet_addr_quick( 192, 168, 137, 1 )   /* WSL host */
#define IPERF_PORT        5001
#define IPERF_SECONDS     15
#define IPERF_TXBUF       8192   /* big chunks per send -> fewer calls, fuller window */

static uint8_t ucTxBuf[ IPERF_TXBUF ];

static void prvIperfClientTask( void *pvParameters )
{
    Socket_t xSock;
    struct freertos_sockaddr xServer;
    int i;
    ( void ) pvParameters;

    /* Let DHCP / the link settle a moment after eNetworkUp. */
    vTaskDelay( pdMS_TO_TICKS( 500 ) );

    for( i = 0; i < IPERF_TXBUF; i++ )
    {
        ucTxBuf[ i ] = ( uint8_t ) ( '0' + ( i % 10 ) );
    }

    xSock = FreeRTOS_socket( FREERTOS_AF_INET, FREERTOS_SOCK_STREAM, FREERTOS_IPPROTO_TCP );
    if( ( xSock == FREERTOS_INVALID_SOCKET ) || ( xSock == NULL ) )
    {
        log_error( "[iperf] socket() failed" );
        vTaskDelete( NULL );
        return;
    }

    /* NB: enlarging the TCP TX window does NOT help here — tested lTxWinSize
     * 12 (default) vs 32: throughput stayed ~10-11 Mbps while RTT inflated
     * 13->38 ms (pure bufferbloat). TCP is drain-rate limited, not window
     * limited: the IP task services a TCP socket with pending TX at usTimeout =
     * 1 tick min (FreeRTOS_TCP_IP.c ~L604), so with the window kept full it
     * sends ~1 ACK-freed segment per OS tick ≈ ~900 pps ≈ 11 Mbps. UDP (no ACK
     * clock) hits 1930 pps / 22.7 Mbps on the same radio, so the cap is the
     * tick-quantized TCP servicing, not the driver. Raising configTICK_RATE_HZ
     * (the direct lever) hard-faults the driver. Left at the default window. */

    ( void ) memset( &xServer, 0, sizeof( xServer ) );
    xServer.sin_family = FREERTOS_AF_INET;
    xServer.sin_port = FreeRTOS_htons( IPERF_PORT );
    xServer.sin_addr = IPERF_SERVER_IP;

    log_info( "[iperf] connecting to 192.168.137.1:%d ...", IPERF_PORT );
    if( FreeRTOS_connect( xSock, &xServer, sizeof( xServer ) ) != 0 )
    {
        log_error( "[iperf] connect FAILED" );
        FreeRTOS_closesocket( xSock );
        vTaskDelete( NULL );
        return;
    }
    log_info( "[iperf] connected; streaming %ds...", IPERF_SECONDS );

    /* Zeroed iperf2 client header (24 bytes): flags = 0 => plain test. */
    {
        static const uint8_t ucHdr[ 24 ] = { 0 };
        FreeRTOS_send( xSock, ucHdr, sizeof( ucHdr ), 0 );
    }

    {
        uint32_t ulTotal = 0;   /* bytes (10 s of WiFi << 4 GB) */
        TickType_t xStart = xTaskGetTickCount();
        TickType_t xMs;
        uint32_t ulKbits;

        while( ( ( xTaskGetTickCount() - xStart ) * portTICK_PERIOD_MS ) < ( IPERF_SECONDS * 1000U ) )
        {
            BaseType_t xSent = FreeRTOS_send( xSock, ucTxBuf, sizeof( ucTxBuf ), 0 );
            if( xSent > 0 )
            {
                ulTotal += ( uint32_t ) xSent;
            }
            else
            {
                log_info( "[iperf] send err %d", (int)xSent );
                break;
            }
        }

        xMs = ( xTaskGetTickCount() - xStart ) * portTICK_PERIOD_MS;
        if( xMs == 0 ) xMs = 1;
        /* kbit/s = bytes-per-ms * 8 — 32-bit, no overflow, no 64-bit divide.
         * DiagPrintf only handles plain %d (no %u / width), so keep it simple. */
        ulKbits = ( ulTotal / xMs ) * 8U;
        log_info( "[iperf] TX %d bytes in %d ms = %d Kbit/s",
                    (int)ulTotal, (int)xMs, (int)ulKbits );
    }

    FreeRTOS_shutdown( xSock, FREERTOS_SHUT_RDWR );
    FreeRTOS_closesocket( xSock );
    vTaskDelete( NULL );
}

void iperf_client_start( void )
{
    static int started = 0;
    if( !started )
    {
        started = 1;
        /* +2 (below the WiFi driver's xmit thread — raising it above starves the
         * actual transmit and throughput drops). */
        xTaskCreate( prvIperfClientTask, "iperf", 1024, NULL, tskIDLE_PRIORITY + 2, NULL );
    }
}
