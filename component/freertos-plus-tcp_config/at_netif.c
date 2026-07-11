/*
 * at_netif.c — the cross-platform FreeRTOS-Plus-TCP STA NetworkInterface helper.
 * See at_netif.h. The per-platform WiFi driver is threaded in via at_netif_ops_t;
 * this file owns the shared boilerplate + the single-per-image stack callbacks.
 */
#include "at_netif.h"
#include "FreeRTOS_IP_Private.h"      /* IPStackEvent_t / xSendEventStructToIPTask */
#include "FreeRTOS_Routing.h"         /* FreeRTOS_FillEndPoint_IPv6 (at_netif_ipv6_init) */
#include "task.h"                     /* xTaskCreate / vTaskDelay (IPv6 MLD task) */
#if ( ipconfigUSE_IPv6 != 0 )
#include "FreeRTOS_ND.h"             /* vRAProcess (SLAAC restart in the MLD task) */
#endif
#include <string.h>                   /* memset / strlen */
#include <strings.h>                  /* strcasecmp / strncasecmp (mDNS name match) */
#include <stdlib.h>                   /* getenv (HOSTNAME override) */
#include <stdio.h>                    /* snprintf (default hostname) */

extern int printf( const char *, ... );

static const at_netif_ops_t * s_ops = NULL;
static NetworkInterface_t    * s_pxMyInterface = NULL;
static volatile BaseType_t     s_ready = pdFALSE;

void at_netif_set_ops( const at_netif_ops_t * ops )
{
    s_ops = ops;
}

void at_netif_set_ready( void )       { s_ready = pdTRUE; }
BaseType_t at_netif_is_ready( void )  { return s_ready; }
NetworkInterface_t * at_netif_get( void ) { return s_pxMyInterface; }

#if ( ipconfigETHERNET_DRIVER_FILTERS_FRAME_TYPES == 0 )
    #define ipCONSIDER_FRAME_FOR_PROCESSING( buf )    eProcessBuffer
#else
    #define ipCONSIDER_FRAME_FOR_PROCESSING( buf )    eConsiderFrameForProcessing( ( buf ) )
#endif

/* ===== NetworkInterface access functions (over the platform ops) =========== */
static BaseType_t prvInitialise( NetworkInterface_t * pxInterface )
{
    ( void ) pxInterface;
    return ( ( s_ops != NULL ) && ( s_ops->running != NULL ) ) ? s_ops->running() : pdTRUE;
}

static BaseType_t prvGetPhyLinkStatus( NetworkInterface_t * pxInterface )
{
    ( void ) pxInterface;
    return ( ( s_ops != NULL ) && ( s_ops->link_up != NULL ) ) ? s_ops->link_up() : pdFALSE;
}

static BaseType_t prvOutput( NetworkInterface_t * pxInterface,
                             NetworkBufferDescriptor_t * const pxNetworkBuffer,
                             BaseType_t xReleaseAfterSend )
{
    BaseType_t xResult = pdFALSE;
    ( void ) pxInterface;

    if( ( s_ops != NULL ) && ( s_ops->tx != NULL ) && ( pxNetworkBuffer != NULL ) &&
        ( pxNetworkBuffer->pucEthernetBuffer != NULL ) &&
        ( pxNetworkBuffer->xDataLength > 0U ) && ( pxNetworkBuffer->xDataLength <= 0xFFFFU ) )
    {
        if( s_ops->tx( pxNetworkBuffer->pucEthernetBuffer,
                       ( uint16_t ) pxNetworkBuffer->xDataLength ) != pdFALSE )
        {
            xResult = pdTRUE;
            iptraceNETWORK_INTERFACE_TRANSMIT();
        }
    }

    if( xReleaseAfterSend != pdFALSE )
    {
        vReleaseNetworkBufferAndDescriptor( pxNetworkBuffer );
    }
    return xResult;
}

NetworkInterface_t * at_netif_fill_descriptor( BaseType_t xEMACIndex, NetworkInterface_t * pxInterface )
{
    ( void ) memset( pxInterface, 0, sizeof( *pxInterface ) );
    pxInterface->pcName             = "wlan0";
    pxInterface->pvArgument         = ( void * ) xEMACIndex;
    pxInterface->pfInitialise       = prvInitialise;
    pxInterface->pfOutput           = prvOutput;
    pxInterface->pfGetPhyLinkStatus = prvGetPhyLinkStatus;
    FreeRTOS_AddNetworkInterface( pxInterface );
    s_pxMyInterface = pxInterface;
    return pxInterface;
}

/* ===== RX (platform allocs+fills, then submits) ============================ */
NetworkBufferDescriptor_t * at_netif_rx_alloc( size_t len )
{
    if( ( s_ready == pdFALSE ) || ( len == 0U ) ||
        ( len > ( size_t ) ( ipconfigNETWORK_MTU + 14 + 4 ) ) )
    {
        return NULL;
    }
    return pxGetNetworkBufferWithDescriptor( len, 0 );
}

void at_netif_rx_submit( NetworkBufferDescriptor_t * pxBuf, size_t len )
{
    if( pxBuf == NULL )
    {
        return;
    }
    pxBuf->xDataLength = len;
    pxBuf->pxInterface = s_pxMyInterface;
    pxBuf->pxEndPoint  = FreeRTOS_MatchingEndpoint( s_pxMyInterface, pxBuf->pucEthernetBuffer );

    if( ipCONSIDER_FRAME_FOR_PROCESSING( pxBuf->pucEthernetBuffer ) == eProcessBuffer )
    {
        IPStackEvent_t xRxEvent = { eNetworkRxEvent, pxBuf };
        if( xSendEventStructToIPTask( &xRxEvent, 0 ) == pdFAIL )
        {
            vReleaseNetworkBufferAndDescriptor( pxBuf );
            iptraceETHERNET_RX_EVENT_LOST();
        }
    }
    else
    {
        vReleaseNetworkBufferAndDescriptor( pxBuf );
    }
}

/* ===== IPv6: add a link-local + a SLAAC-global endpoint (shared by all platforms;
 * called from at_netif_bringup for Bouffalo and from AmebaD's own bring-up). Must run
 * before FreeRTOS_IPInit_Multi so the endpoints exist when the IP task starts. ======= */
void at_netif_ipv6_init( void )
{
    static NetworkEndPoint_t xEndPoint6_LinkLocal;
    static NetworkEndPoint_t xEndPoint6_Global;
    static BaseType_t        xDone = pdFALSE;
    NetworkInterface_t     * pxIf = at_netif_get();
    NetworkEndPoint_t      * pxIPv4;
    IPv6_Address_t           xLinkLocal, xLLPrefix, xGlobal, xGlobalPrefix, xGateway;
    uint8_t                  mac[ 6 ];

    if( ( xDone != pdFALSE ) || ( pxIf == NULL ) )
    {
        return;
    }
    pxIPv4 = FreeRTOS_FirstEndPoint( pxIf );   /* require the IPv4 endpoint (interface is up) */
    if( pxIPv4 == NULL )
    {
        return;
    }
    at_netif_wifi_mac( mac );   /* WiFi MAC -> EUI-64 link-local (== the interface L2 MAC) */

    ( void ) memset( xLinkLocal.ucBytes, 0, ipSIZE_OF_IPv6_ADDRESS );
    xLinkLocal.ucBytes[ 0 ]  = 0xfeU;
    xLinkLocal.ucBytes[ 1 ]  = 0x80U;
    xLinkLocal.ucBytes[ 8 ]  = ( uint8_t ) ( mac[ 0 ] ^ 0x02U );   /* EUI-64 */
    xLinkLocal.ucBytes[ 9 ]  = mac[ 1 ];
    xLinkLocal.ucBytes[ 10 ] = mac[ 2 ];
    xLinkLocal.ucBytes[ 11 ] = 0xffU;
    xLinkLocal.ucBytes[ 12 ] = 0xfeU;
    xLinkLocal.ucBytes[ 13 ] = mac[ 3 ];
    xLinkLocal.ucBytes[ 14 ] = mac[ 4 ];
    xLinkLocal.ucBytes[ 15 ] = mac[ 5 ];
    ( void ) memset( xLLPrefix.ucBytes, 0, ipSIZE_OF_IPv6_ADDRESS );
    xLLPrefix.ucBytes[ 0 ] = 0xfeU;
    xLLPrefix.ucBytes[ 1 ] = 0x80U;
    FreeRTOS_FillEndPoint_IPv6( pxIf, &xEndPoint6_LinkLocal, &xLinkLocal, &xLLPrefix,
                                10U, NULL, NULL, mac );

    ( void ) memset( xGlobal.ucBytes, 0, ipSIZE_OF_IPv6_ADDRESS );
    ( void ) memset( xGlobalPrefix.ucBytes, 0, ipSIZE_OF_IPv6_ADDRESS );
    ( void ) memset( xGateway.ucBytes, 0, ipSIZE_OF_IPv6_ADDRESS );
    FreeRTOS_FillEndPoint_IPv6( pxIf, &xEndPoint6_Global, &xGlobal, &xGlobalPrefix,
                                64U, &xGateway, NULL, mac );
    xEndPoint6_Global.bits.bWantRA = pdTRUE_UNSIGNED;   /* SLAAC via Router Advertisement */

    xDone = pdTRUE;
}

/* ===== One-shot bring-up (internal storage; for platforms that don't keep
 * their own interface pointer) ============================================== */
void at_netif_bringup( void )
{
    static NetworkInterface_t xInterface;
    static NetworkEndPoint_t  xEndPoint;
    static BaseType_t         xDone = pdFALSE;
    uint8_t ucMAC[ 6 ] = { 0 };
    static const uint8_t ucZero[ 4 ] = { 0, 0, 0, 0 };   /* IP/mask/gw/dns = 0 -> DHCP */

    if( xDone != pdFALSE )
    {
        return;
    }
    xDone = pdTRUE;

    if( ( s_ops != NULL ) && ( s_ops->mac_get != NULL ) )
    {
        s_ops->mac_get( ucMAC );
    }

    at_netif_fill_descriptor( 0, &xInterface );
    FreeRTOS_FillEndPoint( &xInterface, &xEndPoint, ucZero, ucZero, ucZero, ucZero, ucMAC );
    xEndPoint.bits.bWantDHCP = pdTRUE_UNSIGNED;

    at_netif_ipv6_init();   /* link-local + SLAAC-global IPv6 endpoints (must precede IPInit) */
    FreeRTOS_IPInit_Multi();
    at_netif_set_ready();
    at_netif_ipv6_mld_start();   /* periodic MLDv2 reports for inbound IPv6 through a snooping bridge */
}

void at_netif_kick( void )
{
    if( s_pxMyInterface != NULL )
    {
        FreeRTOS_NetworkDown( s_pxMyInterface );
    }
}

/* ===== Single-per-image stack callbacks (over the ops) ===================== */
BaseType_t xApplicationGetRandomNumber( uint32_t * pulNumber )
{
    if( ( s_ops != NULL ) && ( s_ops->get_random != NULL ) )
    {
        return s_ops->get_random( pulNumber );
    }
    *pulNumber = 0U;
    return pdFALSE;
}

uint32_t ulApplicationGetNextSequenceNumber( uint32_t ulSourceAddress, uint16_t usSourcePort,
                                             uint32_t ulDestinationAddress, uint16_t usDestinationPort )
{
    uint32_t v = 0;
    ( void ) ulSourceAddress; ( void ) usSourcePort;
    ( void ) ulDestinationAddress; ( void ) usDestinationPort;
    if( ( s_ops != NULL ) && ( s_ops->get_random != NULL ) )
    {
        ( void ) s_ops->get_random( &v );
    }
    return v;
}

/* Shared fallback-hostname resolver (every platform's prvHostname wraps this). Returns the
 * HOSTNAME env override if set (pre-seeded via lfs.bin KV/HOSTNAME or `env set HOSTNAME ...`),
 * else "<platform_name>_<mac[3]><mac[4]><mac[5]>" from the Bluetooth MAC, formatted once into a
 * static buffer (at_netif_bt_mac is read at most once, so an expensive source -- e.g. AmebaD
 * efuse-over-TF-M -- isn't paid on every mDNS query). */
const char * at_netif_hostname( void )
{
    static char dflt[ 32 ] = { 0 };
    static char host[ 32 ];
    const char * env;

    if( dflt[ 0 ] == '\0' )
    {
        uint8_t mac[ 6 ] = { 0 };
        at_netif_bt_mac( mac );
        ( void ) snprintf( dflt, sizeof( dflt ), "%s_%02x%02x%02x",
                           at_netif_platform_name(), mac[ 3 ], mac[ 4 ], mac[ 5 ] );
    }

    env = getenv( "HOSTNAME" );
    if( ( env != NULL ) && ( env[ 0 ] != '\0' ) )
    {
        ( void ) strncpy( host, env, sizeof( host ) - 1U );
        host[ sizeof( host ) - 1U ] = '\0';
        return host;
    }
    return dflt;
}

const char * pcApplicationHostnameHook( void )
{
    if( ( s_ops != NULL ) && ( s_ops->hostname != NULL ) )
    {
        return s_ops->hostname();
    }
    return "at-device";
}

BaseType_t xApplicationDNSQueryHook( const char * pcName )
{
    const char * h  = pcApplicationHostnameHook();
    size_t       hl = strlen( h );

    if( strcasecmp( pcName, h ) == 0 )
    {
        return pdPASS;
    }
    if( ( strncasecmp( pcName, h, hl ) == 0 ) && ( strcasecmp( &pcName[ hl ], ".local" ) == 0 ) )
    {
        return pdPASS;
    }
    return pdFAIL;
}

/* ===== Shared network-up/down body ========================================
 * Both platforms' STRONG vApplicationIPNetworkEventHook do only their platform-specific dedup +
 * driver signalling, then call these. Prints the DHCP config uniformly ("[NET]" tag) so bl602 and
 * AmebaD emit identical network-up lines, and fans out to app_on_network_up(). */
extern int      snprintf( char *, size_t, const char *, ... );
extern char   * getenv( const char * );
extern uint32_t ulDHCPNtpServerAddress;      /* DHCP option 42, captured by the FreeRTOS_DHCP.c patch */
extern void     app_on_network_up( void );   /* app_startup seam: SNTP + AWS/telemetry (once-guarded) */

static void at_fmt_ipv4( uint32_t ip, char * b, size_t n )
{
    ( void ) snprintf( b, n, "%u.%u.%u.%u",
                       ( unsigned ) ( ip & 0xffU ), ( unsigned ) ( ( ip >> 8 ) & 0xffU ),
                       ( unsigned ) ( ( ip >> 16 ) & 0xffU ), ( unsigned ) ( ( ip >> 24 ) & 0xffU ) );
}

/* ===== IPv6 logging + MLDv2-snooping interop (shared; moved here from amebad_app.cpp so bl602 gets
 * it too) ==================================================================================== */
#if ( ipconfigUSE_IPv6 != 0 )
/* Log every configured IPv6 address on the interface (the fe80:: link-local is set at bring-up; the
 * bWantRA global appears later, once SLAAC completes). Full form (no :: compression) so it can be
 * pasted straight into a ping6. at_netif_on_network_up() calls it as "network UP". */
void at_netif_log_ipv6( const char * pcTag )
{
    NetworkEndPoint_t * px;
    for( px = FreeRTOS_FirstEndPoint( NULL ); px != NULL; px = FreeRTOS_NextEndPoint( NULL, px ) )
    {
        const uint8_t * a = px->ipv6_settings.xIPAddress.ucBytes;
        int i, zero = 1;
        if( px->bits.bIPv6 == 0 ) { continue; }
        for( i = 0; i < ipSIZE_OF_IPv6_ADDRESS; i++ ) { if( a[ i ] != 0 ) { zero = 0; break; } }
        if( zero ) { continue; }   /* global endpoint before SLAAC: address is still :: */
        printf( "[NET] IPv6 (%s) %s %x:%x:%x:%x:%x:%x:%x:%x\r\n", pcTag,
                ( ( a[ 0 ] == 0xfeU ) && ( ( a[ 1 ] & 0xc0U ) == 0x80U ) ) ? "link-local" : "global",
                ( a[ 0 ] << 8 ) | a[ 1 ],   ( a[ 2 ] << 8 ) | a[ 3 ],
                ( a[ 4 ] << 8 ) | a[ 5 ],   ( a[ 6 ] << 8 ) | a[ 7 ],
                ( a[ 8 ] << 8 ) | a[ 9 ],   ( a[ 10 ] << 8 ) | a[ 11 ],
                ( a[ 12 ] << 8 ) | a[ 13 ], ( a[ 14 ] << 8 ) | a[ 15 ] );
    }
}

/* 1 once the bWantRA global endpoint has a non-zero (SLAAC-autoconfigured) address. */
int at_netif_ipv6_global_set( void )
{
    NetworkEndPoint_t * px;
    for( px = FreeRTOS_FirstEndPoint( NULL ); px != NULL; px = FreeRTOS_NextEndPoint( NULL, px ) )
    {
        const uint8_t * a = px->ipv6_settings.xIPAddress.ucBytes;
        if( ( px->bits.bIPv6 != 0 ) && ( a[ 0 ] != 0xfeU ) )   /* IPv6, not the fe80:: link-local */
        {
            int i;
            for( i = 0; i < ipSIZE_OF_IPv6_ADDRESS; i++ ) { if( a[ i ] != 0 ) { return 1; } }
        }
    }
    return 0;
}

static NetworkEndPoint_t * ipv6_linklocal_endpoint( void )
{
    NetworkEndPoint_t * px;
    for( px = FreeRTOS_FirstEndPoint( NULL ); px != NULL; px = FreeRTOS_NextEndPoint( NULL, px ) )
    {
        const uint8_t * a = px->ipv6_settings.xIPAddress.ucBytes;
        if( ( px->bits.bIPv6 != 0 ) && ( a[ 0 ] == 0xfeU ) && ( ( a[ 1 ] & 0xc0U ) == 0x80U ) ) { return px; }
    }
    return NULL;
}

static NetworkEndPoint_t * ipv6_ra_endpoint( void )
{
    NetworkEndPoint_t * px;
    for( px = FreeRTOS_FirstEndPoint( NULL ); px != NULL; px = FreeRTOS_NextEndPoint( NULL, px ) )
    {
        if( ( px->bits.bIPv6 != 0 ) && ( px->bits.bWantRA != 0 ) ) { return px; }
    }
    return NULL;
}

/* IPv6 + MLD-snooping interop: the router's bridge forwards an IPv6 multicast group only to ports that
 * announced membership via MLD; FreeRTOS+TCP has no MLD, so the bridge drops the router's Neighbor
 * Solicitation for us -> inbound is 100% loss until we emit an MLDv2 report. Hand-crafted: IPv6 +
 * Hop-by-Hop Router-Alert + ICMPv6 type 143, to ff02::16. */
static uint16_t icmpv6_checksum( const uint8_t * src, const uint8_t * dst, const uint8_t * msg, uint16_t len )
{
    uint32_t sum = 0;
    int i;
    for( i = 0; i < 16; i += 2 ) { sum += ( ( uint32_t ) src[ i ] << 8 ) | src[ i + 1 ]; }
    for( i = 0; i < 16; i += 2 ) { sum += ( ( uint32_t ) dst[ i ] << 8 ) | dst[ i + 1 ]; }
    sum += len;          /* upper-layer length */
    sum += 58U;          /* next header = ICMPv6 */
    for( i = 0; ( i + 1 ) < ( int ) len; i += 2 ) { sum += ( ( uint32_t ) msg[ i ] << 8 ) | msg[ i + 1 ]; }
    if( ( len & 1U ) != 0U ) { sum += ( uint32_t ) msg[ len - 1 ] << 8; }
    while( ( sum >> 16 ) != 0U ) { sum = ( sum & 0xffffU ) + ( sum >> 16 ); }
    return ( uint16_t ) ( ~sum );
}

/* Emit one MLDv2 report (single group record, CHANGE_TO_EXCLUDE = join) for `group`, to ff02::16.
 * One group per report because RouterOS's MLD snooping only honours the first record. */
static void send_one_mld( const uint8_t group[ 16 ] )
{
    NetworkEndPoint_t *          px = ipv6_linklocal_endpoint();
    NetworkBufferDescriptor_t *  pxBuf;
    static const uint8_t FF02_16[ 16 ] = { 0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x16 };
    const uint16_t icmp_len = 8U + 20U;        /* MLDv2 hdr(8) + 1 group record(20) */
    const uint16_t plen     = 8U + icmp_len;   /* Hop-by-Hop(8) + ICMPv6 */
    const uint16_t total    = 14U + 40U + plen;
    const uint8_t * ll;
    uint8_t *       b;
    uint16_t        ck;

    if( ( px == NULL ) || ( px->pxNetworkInterface == NULL ) ) { return; }
    pxBuf = pxGetNetworkBufferWithDescriptor( total, 0 );
    if( pxBuf == NULL ) { return; }
    ll = px->ipv6_settings.xIPAddress.ucBytes;
    b  = pxBuf->pucEthernetBuffer;
    ( void ) memset( b, 0, total );

    b[ 0 ] = 0x33; b[ 1 ] = 0x33; b[ 5 ] = 0x16;                      /* Eth dst 33:33:00:00:00:16 */
    ( void ) memcpy( &b[ 6 ], px->xMACAddress.ucBytes, 6 );           /* Eth src our MAC */
    b[ 12 ] = 0x86; b[ 13 ] = 0xdd;                                  /* ethertype IPv6 */
    b[ 14 ] = 0x60;                                                  /* IPv6 ver 6 */
    b[ 18 ] = ( uint8_t ) ( plen >> 8 ); b[ 19 ] = ( uint8_t ) plen;  /* payload length */
    b[ 20 ] = 0; b[ 21 ] = 1;                                        /* next=Hop-by-Hop, hop limit 1 */
    ( void ) memcpy( &b[ 22 ], ll, 16 );                             /* src LL */
    ( void ) memcpy( &b[ 38 ], FF02_16, 16 );                        /* dst ff02::16 */
    b[ 54 ] = 58; b[ 56 ] = 5; b[ 57 ] = 2; b[ 60 ] = 1;            /* Hop-by-Hop Router-Alert(MLD)+PadN */
    b[ 62 ] = 143; b[ 69 ] = 1;                                      /* MLDv2 report, 1 record */
    b[ 70 ] = 4;                                                     /* record: CHANGE_TO_EXCLUDE, 0 src */
    ( void ) memcpy( &b[ 74 ], group, 16 );                          /* multicast address */
    ck = icmpv6_checksum( ll, FF02_16, &b[ 62 ], icmp_len );
    b[ 64 ] = ( uint8_t ) ( ck >> 8 ); b[ 65 ] = ( uint8_t ) ck;

    pxBuf->xDataLength = total;
    pxBuf->pxEndPoint  = px;
    ( void ) px->pxNetworkInterface->pfOutput( px->pxNetworkInterface, pxBuf, pdTRUE );
}

/* Announce our IPv6 multicast group memberships so the snooping bridge forwards the matching traffic:
 * solicited-node (NS), all-nodes ff02::1 (RA -> SLAAC global), ff02::fb (v6 mDNS). */
static void send_mld_report( void )
{
    NetworkEndPoint_t *  px = ipv6_linklocal_endpoint();
    const uint8_t *      ll;
    uint8_t              solnode[ 16 ]  = { 0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0xff, 0, 0, 0 };
    static const uint8_t allnodes[ 16 ] = { 0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01 };
    static const uint8_t mdns[ 16 ]     = { 0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xfb };

    if( px == NULL ) { return; }
    ll = px->ipv6_settings.xIPAddress.ucBytes;
    solnode[ 13 ] = ll[ 13 ]; solnode[ 14 ] = ll[ 14 ]; solnode[ 15 ] = ll[ 15 ];
    /* Space the sends: pfOutput releases the buffer only after the async driver TX, so three
     * back-to-back allocs can starve the pool and silently drop the 2nd/3rd. */
    send_one_mld( solnode );
    vTaskDelay( pdMS_TO_TICKS( 500 ) );
    send_one_mld( allnodes );
    vTaskDelay( pdMS_TO_TICKS( 500 ) );
    send_one_mld( mdns );

    {
        NetworkEndPoint_t * pxg = ipv6_ra_endpoint();
        if( pxg != NULL )
        {
            const uint8_t * g = pxg->ipv6_settings.xIPAddress.ucBytes;
            if( ( g[ 0 ] | g[ 1 ] ) != 0U )   /* a real (non-zero-prefix) global */
            {
                uint8_t gsol[ 16 ] = { 0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0xff, 0, 0, 0 };
                gsol[ 13 ] = g[ 13 ]; gsol[ 14 ] = g[ 14 ]; gsol[ 15 ] = g[ 15 ];
                vTaskDelay( pdMS_TO_TICKS( 500 ) );
                send_one_mld( gsol );
            }
        }
    }
}

static void ipv6_mld_task( void * pvArg )
{
    int n = 0;
    ( void ) pvArg;
    for( ; ; )
    {
        if( ( FreeRTOS_IsNetworkUp() != pdFALSE ) && ( ipv6_linklocal_endpoint() != NULL ) )
        {
            send_mld_report();
            if( ++n == 1 )
            {
                printf( "[NET] IPv6 MLDv2 report -> ff02::16 (join solicited-node + all-nodes + mDNS; snooping bridge now forwards NS/RA/mDNS)\r\n" );
            }
            else if( n == 2 )
            {
                /* The bridge has learned our groups -> re-run SLAAC so the router's RA reaches us and
                 * the global forms (the first RA process may have timed out before the MLD join). */
                NetworkEndPoint_t * ep = ipv6_ra_endpoint();
                if( ep != NULL ) { vRAProcess( pdTRUE, ep ); }
            }
            else if( n == 3 )
            {
                at_netif_log_ipv6( "after SLAAC restart" );
            }
        }
        vTaskDelay( pdMS_TO_TICKS( ( n < 3 ) ? 2000 : 60000 ) );   /* burst 3x@2s, then 60s upkeep */
    }
}

/* Start the periodic MLDv2 reporter (idempotent). Called from at_netif_bringup (bl602) and AmebaD's
 * WiFi bring-up. Both platforms now get inbound-IPv6 through an MLD-snooping bridge. */
void at_netif_ipv6_mld_start( void )
{
    static BaseType_t xStarted = pdFALSE;
    if( xStarted != pdFALSE ) { return; }
    xStarted = pdTRUE;
    ( void ) xTaskCreate( ipv6_mld_task, "ip6mld", 512, NULL, tskIDLE_PRIORITY + 1, NULL );
}
#else  /* !ipconfigUSE_IPv6 */
void at_netif_log_ipv6( const char * pcTag ) { ( void ) pcTag; }
int  at_netif_ipv6_global_set( void )        { return 0; }
void at_netif_ipv6_mld_start( void )         { }
#endif /* ipconfigUSE_IPv6 */

/* The IPv4 endpoint (first non-IPv6 endpoint with a lease) for the DNS-server config. */
static NetworkEndPoint_t * at_netif_ipv4_endpoint( void )
{
    NetworkEndPoint_t * px;
    for( px = FreeRTOS_FirstEndPoint( NULL ); px != NULL; px = FreeRTOS_NextEndPoint( NULL, px ) )
    {
#if ( ipconfigUSE_IPv6 != 0 )
        if( px->bits.bIPv6 != 0U ) { continue; }
#endif
        if( px->ipv4_settings.ulIPAddress != 0U ) { return px; }
    }
    return NULL;
}

void at_netif_on_network_up( void )
{
    uint32_t ulIP = 0, ulMask = 0, ulGW = 0, ulDNS = 0;
    NetworkEndPoint_t * ep;
    char b[ 16 ];

    FreeRTOS_GetAddressConfiguration( &ulIP, &ulMask, &ulGW, &ulDNS );
    printf( "[NET] ==== network UP ====\r\n" );
    at_netif_log_ipv6( "network UP" );

    at_fmt_ipv4( ulIP,   b, sizeof( b ) ); printf( "[NET] DHCP IP   %s\r\n", b );
    at_fmt_ipv4( ulMask, b, sizeof( b ) ); printf( "[NET] netmask   %s\r\n", b );
    at_fmt_ipv4( ulGW,   b, sizeof( b ) ); printf( "[NET] gateway   %s\r\n", b );

    ep = at_netif_ipv4_endpoint();
    if( ep != NULL )
    {
        unsigned     i;
        const char * d1 = getenv( "DNS_SERVER" );
        const char * d2 = getenv( "DNS_SERVER2" );
        for( i = 0; i < ( unsigned ) ipconfigENDPOINT_DNS_ADDRESS_COUNT; i++ )
        {
            uint32_t d = ep->ipv4_settings.ulDNSServerAddresses[ i ];
            if( d != 0U ) { at_fmt_ipv4( d, b, sizeof( b ) ); printf( "[NET] DHCP DNS[%u] %s\r\n", i, b ); }
        }
        /* Optional static IPv4 DNS override (env). An IPv6 literal yields inet_addr()==0 -> skip, so a
         * c-ares IPv6 DNS_SERVER (CARES=ON) doesn't wipe the DHCP-learned IPv4 resolver. */
        {
            uint32_t a1 = ( ( d1 != NULL ) && ( d1[ 0 ] != '\0' ) ) ? FreeRTOS_inet_addr( d1 ) : 0U;
            if( a1 != 0U )
            {
                ep->ipv4_settings.ulDNSServerAddresses[ 0 ] = a1;
                if( ( d2 != NULL ) && ( d2[ 0 ] != '\0' ) )
                {
                    uint32_t a2 = FreeRTOS_inet_addr( d2 );
                    if( a2 != 0U ) { ep->ipv4_settings.ulDNSServerAddresses[ 1 ] = a2; }
                }
                printf( "[NET] DNS override -> %s / %s\r\n", d1, ( d2 != NULL ) ? d2 : "-" );
            }
        }
    }
    if( ulDHCPNtpServerAddress != 0U ) { at_fmt_ipv4( ulDHCPNtpServerAddress, b, sizeof( b ) ); printf( "[NET] DHCP NTP    %s\r\n", b ); }
    else                               { printf( "[NET] DHCP NTP    (none - DHCP option 42 not received)\r\n" ); }

    app_on_network_up();
}

void at_netif_on_network_down( void )
{
    printf( "[NET] ==== network DOWN ====\r\n" );
    ulDHCPNtpServerAddress = 0U;   /* re-derive from the next network's DHCP */
}

/* Weak: bl616 etc. that don't strong-override. bl602 + AmebaD provide their own strong hook
 * (platform dedup) that also calls at_netif_on_network_up/down. */
__attribute__( ( weak ) )
void vApplicationIPNetworkEventHook( eIPCallbackEvent_t eNetworkEvent )
{
    if( eNetworkEvent == eNetworkUp )
    {
        NetworkEndPoint_t * px = FreeRTOS_FirstEndPoint( NULL );
        uint32_t ulIP = ( px != NULL ) ? px->ipv4_settings.ulIPAddress : 0U;
        printf( "[TCP] network up: %u.%u.%u.%u\r\n",
                ( unsigned ) ( ulIP & 0xffU ), ( unsigned ) ( ( ulIP >> 8 ) & 0xffU ),
                ( unsigned ) ( ( ulIP >> 16 ) & 0xffU ), ( unsigned ) ( ( ulIP >> 24 ) & 0xffU ) );
        if( ( s_ops != NULL ) && ( s_ops->on_got_ip != NULL ) )
        {
            s_ops->on_got_ip( ulIP );
        }
    }
    else
    {
        printf( "[TCP] network down\r\n" );
    }
}

/* Drive the platform's got-IP op (e.g. bl602 wifi_mgmr_api_ip_got) from an app-provided STRONG
 * vApplicationIPNetworkEventHook that replaces the weak one above but still owns the driver signal.
 * bl602's wifi_mgmr holds a "wifiConnected_ipObtaining" state it only leaves on this cue; FreeRTOS+TCP
 * runs DHCP, so without it the driver hits its ip-obtaining timeout, self-disconnects, and the link
 * churns. Safe no-op if no ops / no on_got_ip are registered (e.g. a platform that doesn't need it). */
void at_netif_signal_got_ip( uint32_t ulIP )
{
    if( ( s_ops != NULL ) && ( s_ops->on_got_ip != NULL ) )
    {
        s_ops->on_got_ip( ulIP );
    }
}
