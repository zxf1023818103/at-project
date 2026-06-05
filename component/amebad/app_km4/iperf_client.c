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
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"

extern void DiagPrintf( const char *, ... );

#define IPERF_SERVER_IP   FreeRTOS_inet_addr_quick( 192, 168, 2, 73 )   /* WSL host */
#define IPERF_PORT        5001
#define IPERF_SECONDS     10
#define IPERF_TXBUF       1460

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
        DiagPrintf( "[KM4] iperf: socket() failed\n\r" );
        vTaskDelete( NULL );
        return;
    }

    ( void ) memset( &xServer, 0, sizeof( xServer ) );
    xServer.sin_family = FREERTOS_AF_INET;
    xServer.sin_port = FreeRTOS_htons( IPERF_PORT );
    xServer.sin_addr = IPERF_SERVER_IP;

    DiagPrintf( "[KM4] iperf: connecting to 192.168.2.73:%d ...\n\r", IPERF_PORT );
    if( FreeRTOS_connect( xSock, &xServer, sizeof( xServer ) ) != 0 )
    {
        DiagPrintf( "[KM4] iperf: connect FAILED\n\r" );
        FreeRTOS_closesocket( xSock );
        vTaskDelete( NULL );
        return;
    }
    DiagPrintf( "[KM4] iperf: connected; streaming %ds...\n\r", IPERF_SECONDS );

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
                DiagPrintf( "[KM4] iperf: send err %d\n\r", (int)xSent );
                break;
            }
        }

        xMs = ( xTaskGetTickCount() - xStart ) * portTICK_PERIOD_MS;
        if( xMs == 0 ) xMs = 1;
        /* kbit/s = bytes-per-ms * 8 — 32-bit, no overflow, no 64-bit divide.
         * DiagPrintf only handles plain %d (no %u / width), so keep it simple. */
        ulKbits = ( ulTotal / xMs ) * 8U;
        DiagPrintf( "[KM4] iperf: TX %d bytes in %d ms = %d Kbit/s\n\r",
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
        xTaskCreate( prvIperfClientTask, "iperf", 1024, NULL, tskIDLE_PRIORITY + 2, NULL );
    }
}
