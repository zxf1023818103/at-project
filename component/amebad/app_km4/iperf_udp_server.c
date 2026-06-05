/*
 * Device-side UDP RX sink for the AmebaD FreeRTOS+TCP bring-up — second
 * bottleneck discriminator (RX direction).
 *
 * Background: UDP TX hit 22.7 Mbps but TCP TX caps at ~11 Mbps ≈ the 1 kHz OS
 * tick, because a TCP socket with pending TX is re-serviced at usTimeout = 1
 * tick min and the send is ACK-clocked. The open question: is the ACK *round
 * trip* inflated by the device's RX path (driver netif_rx -> recv task ->
 * ethernetif_recv -> IP task) being tick-quantized? If each received frame
 * incurs ~1 tick of delivery latency, the ACK clock runs at tick rate and caps
 * TCP at window/RTT regardless of anything on the TX side.
 *
 * This sink lets us measure the device's raw UDP RX rate, independent of TCP:
 *     host:   iperf -u -c <device-ip> -b 40M -t 15 -i 1
 *   - device RX caps near ~1000 pps  => RX delivery IS tick-quantized -> that's
 *     the ACK-clock governor; fixing the driver RX wakeup path unlocks TCP.
 *   - device RX runs ~1900+ pps      => RX path is fine; the tick quantization
 *     is purely in FreeRTOS+TCP's TX send-servicing (usTimeout reload).
 *
 * The device binds UDP :5001, counts bytes/packets from the first datagram to
 * the last, and self-reports when the host stops (3 s recv-timeout). It does not
 * parse the iperf2 header — it only needs the receive rate.
 */
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"

extern void DiagPrintf( const char *, ... );

#define IPERF_PORT        5001
#define IPERF_RXBUF       2048   /* > one 1470-byte iperf UDP datagram */

static uint8_t ucRxBuf[ IPERF_RXBUF ];

static void prvIperfUdpServerTask( void *pvParameters )
{
    Socket_t xSock;
    struct freertos_sockaddr xBind;
    TickType_t xRecvTimeout = pdMS_TO_TICKS( 3000 );
    ( void ) pvParameters;

    xSock = FreeRTOS_socket( FREERTOS_AF_INET, FREERTOS_SOCK_DGRAM, FREERTOS_IPPROTO_UDP );
    if( ( xSock == FREERTOS_INVALID_SOCKET ) || ( xSock == NULL ) )
    {
        DiagPrintf( "[KM4] iperf-udp-srv: socket() failed\n\r" );
        vTaskDelete( NULL );
        return;
    }

    FreeRTOS_setsockopt( xSock, 0, FREERTOS_SO_RCVTIMEO, &xRecvTimeout, sizeof( xRecvTimeout ) );

    ( void ) memset( &xBind, 0, sizeof( xBind ) );
    xBind.sin_family = FREERTOS_AF_INET;
    xBind.sin_port = FreeRTOS_htons( IPERF_PORT );
    if( FreeRTOS_bind( xSock, &xBind, sizeof( xBind ) ) != 0 )
    {
        DiagPrintf( "[KM4] iperf-udp-srv: bind() failed\n\r" );
        FreeRTOS_closesocket( xSock );
        vTaskDelete( NULL );
        return;
    }

    DiagPrintf( "[KM4] iperf-udp-srv: listening UDP :%d (run `iperf -u -c <ip> -b 40M -t 15`)...\n\r",
                IPERF_PORT );

    {
        uint32_t ulTotal = 0;
        uint32_t ulPkts = 0;
        uint32_t ulNextReport = 1000;   /* periodic running pps so we don't depend on blast end-timing */
        int      started = 0;
        TickType_t xStart = 0, xLast = 0, xBoot = xTaskGetTickCount();

        for( ;; )
        {
            struct freertos_sockaddr xFrom;
            socklen_t xFromLen = sizeof( xFrom );
            BaseType_t xN = FreeRTOS_recvfrom( xSock, ucRxBuf, sizeof( ucRxBuf ), 0,
                                               &xFrom, &xFromLen );
            if( xN > 0 )
            {
                if( !started )
                {
                    started = 1;
                    xStart = xTaskGetTickCount();
                    DiagPrintf( "[KM4] iperf-udp-srv: first datagram in (%d bytes)\n\r", ( int ) xN );
                }
                xLast = xTaskGetTickCount();
                ulTotal += ( uint32_t ) xN;
                ulPkts++;
                /* Running report every 1000 datagrams (~0.5-1 s of blast) — any
                 * blast/sniff overlap yields a pps number without needing the
                 * host to stop cleanly. */
                if( ulPkts >= ulNextReport )
                {
                    TickType_t xMs = ( xLast - xStart ) * portTICK_PERIOD_MS;
                    if( xMs == 0 ) xMs = 1;
                    DiagPrintf( "[KM4] iperf-udp-srv: RX running %d pkts in %d ms = %d Kbit/s, %d pps\n\r",
                                ( int ) ulPkts, ( int ) xMs,
                                ( int ) ( ( ulTotal / xMs ) * 8U ),
                                ( int ) ( ( ulPkts * 1000U ) / xMs ) );
                    ulNextReport += 2000;
                }
            }
            else
            {
                /* recv timeout */
                if( started )
                {
                    break;   /* host finished the blast */
                }
                /* still waiting for the first datagram — give up after ~90 s */
                if( ( ( xTaskGetTickCount() - xBoot ) * portTICK_PERIOD_MS ) > 90000U )
                {
                    DiagPrintf( "[KM4] iperf-udp-srv: no data in 90s, giving up\n\r" );
                    FreeRTOS_closesocket( xSock );
                    vTaskDelete( NULL );
                    return;
                }
            }
        }

        {
            TickType_t xMs = ( xLast - xStart ) * portTICK_PERIOD_MS;
            uint32_t ulKbits, ulPps;
            if( xMs == 0 ) xMs = 1;
            ulKbits = ( ulTotal / xMs ) * 8U;
            ulPps   = ( ulPkts * 1000U ) / xMs;
            DiagPrintf( "[KM4] iperf-udp-srv: RX %d bytes / %d pkts in %d ms = %d Kbit/s, %d pps\n\r",
                        ( int ) ulTotal, ( int ) ulPkts, ( int ) xMs,
                        ( int ) ulKbits, ( int ) ulPps );
        }
    }

    FreeRTOS_closesocket( xSock );
    vTaskDelete( NULL );
}

void iperf_udp_server_start( void )
{
    static int started = 0;
    if( !started )
    {
        started = 1;
        xTaskCreate( prvIperfUdpServerTask, "iperfudps", 1024, NULL, tskIDLE_PRIORITY + 2, NULL );
    }
}
