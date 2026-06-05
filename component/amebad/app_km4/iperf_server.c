/*
 * Minimal iperf2 TCP sink-server for the RX (download) throughput test:
 * device listens on :5001, an `iperf -c <device-ip> -t 15` on the WSL host
 * streams to it, the device counts bytes and prints the rate. RX is usually
 * faster than the device's TX (the Linux sender pushes hard; the device only
 * has to drain). Started from eNetworkUp.
 */
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"

extern void DiagPrintf( const char *, ... );

#define IPERF_PORT      5001
#define IPERF_RXBUF     ( 4 * 1460 )

static uint8_t ucRecvBuf[ IPERF_RXBUF ];

static void prvIperfServerTask( void *pvParameters )
{
    Socket_t xListen, xConn;
    struct freertos_sockaddr xBind, xClient;
    socklen_t xClientLen = sizeof( xClient );
    const BaseType_t xReuse = pdTRUE;
    const TickType_t xRecvTimeout = pdMS_TO_TICKS( 6000 );
    ( void ) pvParameters;

    xListen = FreeRTOS_socket( FREERTOS_AF_INET, FREERTOS_SOCK_STREAM, FREERTOS_IPPROTO_TCP );
    if( ( xListen == FREERTOS_INVALID_SOCKET ) || ( xListen == NULL ) )
    {
        DiagPrintf( "[KM4] iperf: socket() failed\n\r" );
        vTaskDelete( NULL );
        return;
    }
    FreeRTOS_setsockopt( xListen, 0, FREERTOS_SO_REUSE_LISTEN_SOCKET, &xReuse, sizeof( xReuse ) );
    ( void ) memset( &xBind, 0, sizeof( xBind ) );
    xBind.sin_family = FREERTOS_AF_INET;
    xBind.sin_port = FreeRTOS_htons( IPERF_PORT );
    FreeRTOS_bind( xListen, &xBind, sizeof( xBind ) );
    FreeRTOS_listen( xListen, 2 );
    DiagPrintf( "[KM4] iperf TCP sink :%d  (host: iperf -c <ip> -p %d -t 15)\n\r", IPERF_PORT, IPERF_PORT );

    for( ; ; )
    {
        xConn = FreeRTOS_accept( xListen, &xClient, &xClientLen );
        if( ( xConn == NULL ) || ( xConn == FREERTOS_INVALID_SOCKET ) )
        {
            continue;
        }
        FreeRTOS_setsockopt( xConn, 0, FREERTOS_SO_RCVTIMEO, &xRecvTimeout, sizeof( xRecvTimeout ) );
        DiagPrintf( "[KM4] iperf: client connected; receiving...\n\r" );

        {
            uint32_t ulTotal = 0;
            TickType_t xStart = 0, xMs;
            uint32_t ulKbits;
            int xFirst = 1;

            for( ; ; )
            {
                BaseType_t xRecv = FreeRTOS_recv( xConn, ucRecvBuf, sizeof( ucRecvBuf ), 0 );
                if( xRecv > 0 )
                {
                    if( xFirst ) { xFirst = 0; xStart = xTaskGetTickCount(); }   /* time from first byte */
                    ulTotal += ( uint32_t ) xRecv;
                }
                else
                {
                    break;
                }
            }
            xMs = ( xTaskGetTickCount() - xStart ) * portTICK_PERIOD_MS;
            if( xMs == 0 ) xMs = 1;
            ulKbits = ( ulTotal / xMs ) * 8U;
            DiagPrintf( "[KM4] iperf: RX %d bytes in %d ms = %d Kbit/s\n\r",
                        (int)ulTotal, (int)xMs, (int)ulKbits );
        }
        FreeRTOS_closesocket( xConn );
    }
}

void iperf_server_start( void )
{
    static int started = 0;
    if( !started )
    {
        started = 1;
        xTaskCreate( prvIperfServerTask, "iperfs", 1024, NULL, tskIDLE_PRIORITY + 2, NULL );
    }
}
