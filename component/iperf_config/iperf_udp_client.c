/*
 * Minimal iperf2 UDP client for the AmebaD FreeRTOS+TCP bring-up — a bottleneck
 * discriminator, NOT a product feature.
 *
 * The TCP TX test plateaus at ~11-12 Mbps ≈ ~968 packets/s, suspiciously close
 * to configTICK_RATE_HZ (1000) — i.e. roughly one packet per OS tick. That could
 * be either (a) the closed Realtek driver pacing each frame ~1 ms regardless of
 * protocol, or (b) TCP-layer pacing (window / delayed-ACK / IP-task event loop).
 * UDP removes the TCP window + ACK feedback entirely, so the rate of successful
 * FreeRTOS_sendto is gated only by network-buffer availability, which is gated
 * by how fast the driver drains frames to the air. So:
 *   - UDP also caps near ~968 pps  => per-frame DRIVER limit (TCP innocent).
 *   - UDP runs much faster         => TCP-stack pacing (worth digging).
 *
 * The device blasts fixed-size datagrams for IPERF_SECONDS and self-reports
 * bytes / packets / Kbit/s / pps. Host side (second, independent read-out):
 *     iperf -u -s -i 1
 * Each datagram carries an iperf2 UDP_datagram header (seq id, tv_sec, tv_usec,
 * big-endian) so `iperf -u -s` reports clean per-second intervals; a few FIN
 * datagrams (negative id) at the end let it print its final summary.
 */
#include "FreeRTOS.h"
#include "at_log.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"


#define IPERF_SERVER_IP   FreeRTOS_inet_addr_quick( 192, 168, 137, 1 )   /* WSL host */
#define IPERF_PORT        5001
#define IPERF_SECONDS     15
#define IPERF_UDP_DGRAM   1470   /* iperf2 default UDP payload; <1472 so 1 frame, no IP frag */

static uint8_t ucDgram[ IPERF_UDP_DGRAM ];

/* Write a 32-bit value big-endian into the datagram's UDP_datagram.id field. */
static void prvPutSeq( int32_t lSeq )
{
    ucDgram[ 0 ] = ( uint8_t )( ( ( uint32_t ) lSeq >> 24 ) & 0xff );
    ucDgram[ 1 ] = ( uint8_t )( ( ( uint32_t ) lSeq >> 16 ) & 0xff );
    ucDgram[ 2 ] = ( uint8_t )( ( ( uint32_t ) lSeq >> 8 ) & 0xff );
    ucDgram[ 3 ] = ( uint8_t )( ( uint32_t ) lSeq & 0xff );
}

static void prvIperfUdpClientTask( void *pvParameters )
{
    Socket_t xSock;
    struct freertos_sockaddr xServer;
    int i;
    ( void ) pvParameters;

    vTaskDelay( pdMS_TO_TICKS( 500 ) );

    /* Fill the payload (after the 12-byte UDP_datagram header) with a pattern. */
    for( i = 0; i < IPERF_UDP_DGRAM; i++ )
    {
        ucDgram[ i ] = ( uint8_t )( '0' + ( i % 10 ) );
    }

    xSock = FreeRTOS_socket( FREERTOS_AF_INET, FREERTOS_SOCK_DGRAM, FREERTOS_IPPROTO_UDP );
    if( ( xSock == FREERTOS_INVALID_SOCKET ) || ( xSock == NULL ) )
    {
        log_error( "[iperf-udp] socket() failed" );
        vTaskDelete( NULL );
        return;
    }

    ( void ) memset( &xServer, 0, sizeof( xServer ) );
    xServer.sin_family = FREERTOS_AF_INET;
    xServer.sin_port = FreeRTOS_htons( IPERF_PORT );
    xServer.sin_addr = IPERF_SERVER_IP;

    log_info( "[iperf-udp] blasting 192.168.2.73:%d for %ds (dgram %d)...",
                IPERF_PORT, IPERF_SECONDS, IPERF_UDP_DGRAM );

    {
        uint32_t ulTotal = 0;     /* bytes successfully sent */
        int32_t  lSeq = 0;        /* datagram sequence / count */
        uint32_t ulStall = 0;     /* sendto returned 0 (no buffer within block time) */
        TickType_t xStart = xTaskGetTickCount();
        TickType_t xMs;
        uint32_t ulKbits, ulPps;

        while( ( ( xTaskGetTickCount() - xStart ) * portTICK_PERIOD_MS ) < ( IPERF_SECONDS * 1000U ) )
        {
            BaseType_t xSent;
            prvPutSeq( lSeq );
            xSent = FreeRTOS_sendto( xSock, ucDgram, IPERF_UDP_DGRAM, 0,
                                     &xServer, sizeof( xServer ) );
            if( xSent == IPERF_UDP_DGRAM )
            {
                ulTotal += ( uint32_t ) xSent;
                lSeq++;
            }
            else
            {
                /* 0 => couldn't get a network buffer within the UDP send block
                 * time: itself a sign the driver is the drain bottleneck. */
                ulStall++;
            }
        }

        xMs = ( xTaskGetTickCount() - xStart ) * portTICK_PERIOD_MS;
        if( xMs == 0 ) xMs = 1;
        /* 32-bit only (no %u, no 64-bit divide). lSeq*1000 fits: even 100k pkts
         * -> 1e8 < 4.29e9. */
        ulKbits = ( ulTotal / xMs ) * 8U;
        ulPps   = ( ( uint32_t ) lSeq * 1000U ) / xMs;
        log_info( "[iperf-udp] TX %d bytes / %d pkts in %d ms = %d Kbit/s, %d pps (stalls %d)",
                    ( int ) ulTotal, ( int ) lSeq, ( int ) xMs,
                    ( int ) ulKbits, ( int ) ulPps, ( int ) ulStall );

        /* FIN: a few negative-id datagrams so `iperf -u -s` prints its summary. */
        for( i = 0; i < 5; i++ )
        {
            prvPutSeq( -lSeq );
            ( void ) FreeRTOS_sendto( xSock, ucDgram, IPERF_UDP_DGRAM, 0,
                                      &xServer, sizeof( xServer ) );
            vTaskDelay( pdMS_TO_TICKS( 2 ) );
        }
    }

    FreeRTOS_closesocket( xSock );
    vTaskDelete( NULL );
}

void iperf_udp_client_start( void )
{
    static int started = 0;
    if( !started )
    {
        started = 1;
        /* +2, same as the TCP client (below the driver's xmit thread). */
        xTaskCreate( prvIperfUdpClientTask, "iperfudp", 1024, NULL, tskIDLE_PRIORITY + 2, NULL );
    }
}
