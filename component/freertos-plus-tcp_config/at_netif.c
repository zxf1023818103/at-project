/*
 * at_netif.c — the cross-platform FreeRTOS-Plus-TCP STA NetworkInterface helper.
 * See at_netif.h. The per-platform WiFi driver is threaded in via at_netif_ops_t;
 * this file owns the shared boilerplate + the single-per-image stack callbacks.
 */
#include "at_netif.h"
#include "FreeRTOS_IP_Private.h"      /* IPStackEvent_t / xSendEventStructToIPTask */
#include "FreeRTOS_Routing.h"         /* FreeRTOS_FillEndPoint_IPv6 (at_netif_ipv6_init) */
#include <string.h>                   /* memset / strlen */
#include <strings.h>                  /* strcasecmp / strncasecmp (mDNS name match) */

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
    const uint8_t          * mac;

    if( ( xDone != pdFALSE ) || ( pxIf == NULL ) )
    {
        return;
    }
    pxIPv4 = FreeRTOS_FirstEndPoint( pxIf );   /* the IPv4 endpoint carries our MAC */
    if( pxIPv4 == NULL )
    {
        return;
    }
    mac = pxIPv4->xMACAddress.ucBytes;

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

/* Weak: bl602/bl616 use this default (print IP + ops->on_got_ip). AmebaD overrides it
 * with a strong definition in app_km4/main.c (IPv6 SLAAC + MLDv2 + network-DOWN timing). */
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
