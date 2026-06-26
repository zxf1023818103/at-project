/*
 * FreeRTOS-Plus-TCP <-> Realtek AmebaD WiFi driver NetworkInterface (legacy
 * single-interface API) + the RX bridge. DHCP-only IPv4 bring-up: get an IP.
 *
 * Data path:
 *   TX  FreeRTOS+TCP IP task -> xNetworkInterfaceOutput() -> rltk_wlan_send()
 *   RX  driver -> netif_rx(idx,len) [vendored lwip_intf.c] -> amebad_tcp_netif_input()
 *       [here] -> copy via rltk_wlan_recv() into a NetworkBuffer ->
 *       xSendEventStructToIPTask(eNetworkRxEvent)
 *
 * The vendored lwip_intf.c is now stack-agnostic: it owns netif_rx /
 * rltk_wlan_send / rltk_wlan_recv and calls the amebad_tcp_* helpers defined
 * here (amebad_tcp_netif_input for RX, amebad_tcp_is_valid_ip for the driver's IP
 * filter, amebad_tcp_get_addr for WOWLAN IP/GW/MASK) — no lwIP types cross the
 * seam, so idx is passed directly (no netif pointer / netif_get_idx). The minimal
 * lwIP-typed xnetif[] the prebuilt driver + wifi_conf.c still need lives in
 * realtek_xnetif.c.
 */
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_IP_Private.h"      /* IPStackEvent_t / xSendEventStructToIPTask */
#include "FreeRTOS_Routing.h"         /* NetworkInterface_t / FreeRTOS_AddNetworkInterface */
#include "NetworkBufferManagement.h"

/* ---- Realtek driver glue (mirror of component/amebad/wlan/osdep/lwip_intf.h) -- */
struct eth_drv_sg { unsigned int buf; unsigned int len; };
extern int  rltk_wlan_send( int idx, struct eth_drv_sg * sg_list, int sg_len, int total_len );
extern void rltk_wlan_recv( int idx, struct eth_drv_sg * sg_list, int sg_len );
extern unsigned char rltk_wlan_running( unsigned char idx );
extern int  wifi_is_connected_to_ap( void );

#define WLAN_STA_IDX    0

/* Force the linker to pull component/amebad/wlan/src/rtw_opt_skbuf.o (our larger TX/RX skb pool: 24
 * data / 26 struct buffers) instead of lib_wlan.a's WEAK 8/10 defaults. That object sits in a static
 * lib and nothing else references it, so without this it's never pulled and the pool silently stays
 * at 8 (effective TX = 6 buffers → the high-rate upload wedge; found via OpenOCD). See rtw_opt_skbuf.c. */
extern int rtw_opt_skbuf_force_link;
__attribute__( ( used ) ) static int * const amebad_pull_skbuf = &rtw_opt_skbuf_force_link;

/* Set pdTRUE once FreeRTOS_IPInit_Multi() has run (buffer pool + IP task exist).
 * The driver starts delivering RX (beacons/probes) during association — i.e.
 * BEFORE the IP stack is up — so the RX bridge must drop (but still drain) frames
 * until the stack is ready, or pxGetNetworkBufferWithDescriptor faults on the
 * uninitialised buffer list. */
volatile BaseType_t xRealtekTcpReady = pdFALSE;

void amebad_freertos_tcp_set_ready( void )
{
    xRealtekTcpReady = pdTRUE;
}

/* Stored when the interface descriptor is filled; RX descriptors are tagged with
 * it (+ the matching endpoint) so the IP task can route incoming frames. */
static NetworkInterface_t * pxMyInterface = NULL;

#if ( ipconfigETHERNET_DRIVER_FILTERS_FRAME_TYPES == 0 )
    #define ipCONSIDER_FRAME_FOR_PROCESSING( buf )    eProcessBuffer
#else
    #define ipCONSIDER_FRAME_FOR_PROCESSING( buf )    eConsiderFrameForProcessing( ( buf ) )
#endif

/* ===== NetworkInterface (V4.0.0 multi-endpoint descriptor model) =========== *
 * The three access functions are registered as function pointers in a
 * NetworkInterface_t descriptor (filled by pxRealtek_FillInterfaceDescriptor),
 * not as fixed global names. */

static BaseType_t prvInitialise( NetworkInterface_t * pxInterface )
{
    ( void ) pxInterface;
    /* The WiFi driver is brought up (wifi_on + wifi_connect) before the IP task
     * starts, so "initialised" == the STA driver is running. */
    return ( rltk_wlan_running( WLAN_STA_IDX ) != 0 ) ? pdTRUE : pdFALSE;
}

static BaseType_t prvGetPhyLinkStatus( NetworkInterface_t * pxInterface )
{
    ( void ) pxInterface;
    return ( wifi_is_connected_to_ap() == 0 ) ? pdTRUE : pdFALSE;   /* 0 == connected */
}

static BaseType_t prvOutput( NetworkInterface_t * pxInterface,
                             NetworkBufferDescriptor_t * const pxNetworkBuffer,
                             BaseType_t xReleaseAfterSend )
{
    struct eth_drv_sg sg;
    BaseType_t xResult = pdFALSE;
    ( void ) pxInterface;

    if( ( pxNetworkBuffer != NULL ) && ( pxNetworkBuffer->pucEthernetBuffer != NULL ) &&
        ( rltk_wlan_running( WLAN_STA_IDX ) != 0 ) )
    {
        sg.buf = ( unsigned int ) pxNetworkBuffer->pucEthernetBuffer;
        sg.len = ( unsigned int ) pxNetworkBuffer->xDataLength;
        /* rltk_wlan_send copies out synchronously; the descriptor can be freed. */
        if( rltk_wlan_send( WLAN_STA_IDX, &sg, 1, ( int ) pxNetworkBuffer->xDataLength ) == 0 )
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

/* Fill + register the interface descriptor. Called from app_km4 before
 * FreeRTOS_IPInit_Multi(). xEMACIndex is the WLAN VIF index (0 = STA). */
NetworkInterface_t * pxRealtek_FillInterfaceDescriptor( BaseType_t xEMACIndex,
                                                        NetworkInterface_t * pxInterface )
{
    ( void ) memset( pxInterface, 0, sizeof( *pxInterface ) );
    pxInterface->pcName = "wlan0";
    pxInterface->pvArgument = ( void * ) xEMACIndex;
    pxInterface->pfInitialise = prvInitialise;
    pxInterface->pfOutput = prvOutput;
    pxInterface->pfGetPhyLinkStatus = prvGetPhyLinkStatus;
    FreeRTOS_AddNetworkInterface( pxInterface );
    pxMyInterface = pxInterface;
    return pxInterface;
}

/* ===== RX bridge ============================================================ *
 * The driver delivers a frame by calling netif_rx(idx,len) (vendored
 * lwip_intf.c), which now calls amebad_tcp_netif_input(idx,len) here (the lwIP
 * port's ethernetif_recv(&xnetif[idx]) is gone). We copy the frame out of the
 * driver into a NetworkBufferDescriptor and hand it to the IP task. NB: we MUST
 * always drain the driver (rltk_wlan_recv) even when dropping, or the driver's
 * RX ring stalls. */
void amebad_tcp_netif_input( int idx, unsigned int len )
{
    NetworkBufferDescriptor_t * pxBuf;
    struct eth_drv_sg sg;
    int total_len = ( int ) len;

    if( ( total_len <= 0 ) || ( total_len > ( int ) ( ipconfigNETWORK_MTU + 14 + 4 ) ) )
    {
        return;
    }

    /* Until FreeRTOS+TCP is initialised, drop frames — but still drain them from
     * the driver (else its RX ring stalls). */
    if( xRealtekTcpReady == pdFALSE )
    {
        static unsigned char ucEarly[ ipconfigNETWORK_MTU + 18 ] __attribute__( ( aligned( 4 ) ) );
        sg.buf = ( unsigned int ) ucEarly;
        sg.len = ( unsigned int ) total_len;
        rltk_wlan_recv( idx, &sg, 1 );
        return;
    }

    pxBuf = pxGetNetworkBufferWithDescriptor( ( size_t ) total_len, 0 );
    if( pxBuf == NULL )
    {
        /* No buffer — still drain the frame from the driver into a throwaway. */
        static unsigned char ucSink[ ipconfigNETWORK_MTU + 18 ] __attribute__( ( aligned( 4 ) ) );
        sg.buf = ( unsigned int ) ucSink;
        sg.len = ( unsigned int ) total_len;
        rltk_wlan_recv( idx, &sg, 1 );
        iptraceETHERNET_RX_EVENT_LOST();
        return;
    }

    sg.buf = ( unsigned int ) pxBuf->pucEthernetBuffer;
    sg.len = ( unsigned int ) total_len;
    rltk_wlan_recv( idx, &sg, 1 );
    pxBuf->xDataLength = ( size_t ) total_len;
    /* Tag the descriptor so the IP task can route it (V4.0.0 multi-endpoint). */
    pxBuf->pxInterface = pxMyInterface;
    pxBuf->pxEndPoint = FreeRTOS_MatchingEndpoint( pxMyInterface, pxBuf->pucEthernetBuffer );

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

/* ===== Driver RX filter + address getters (called from lwip_intf.c) ======== *
 * Replace lwip_intf.c's old lwIP-xnetif lookups. The driver asks "is this dest
 * IP for us?" (netif_is_valid_IP) and WOWLAN asks for IP/GW/MASK; both now read
 * the FreeRTOS+TCP endpoint. Filtering errs toward ACCEPT — dropping a valid
 * frame breaks RX, whereas accepting an extra one only costs the IP task a
 * routing check it would do anyway. */
int amebad_tcp_is_valid_ip( int idx, const unsigned char * ip_dest )
{
    uint32_t ulDest = 0, ulOurIP = 0, ulMask = 0;
    NetworkEndPoint_t * pxEP;
    ( void ) idx;

    if( ip_dest == NULL )
    {
        return 1;
    }
    ( void ) memcpy( &ulDest, ip_dest, sizeof( ulDest ) );   /* network byte order */

    pxEP = ( pxMyInterface != NULL ) ? FreeRTOS_FirstEndPoint( pxMyInterface ) : NULL;
    if( pxEP != NULL )
    {
        ulOurIP = pxEP->ipv4_settings.ulIPAddress;
        ulMask  = pxEP->ipv4_settings.ulNetMask;
    }

    /* ip_dest is only 4 bytes with no address family, so this hook cannot tell an IPv6 unicast or
     * multicast destination (which we must accept - the RA/NDP and our own global traffic) from an
     * IPv4 address. The driver's MAC-level RX filter already gates frames by destination MAC
     * (unicast-to-us + broadcast + subscribed multicast), so everything reaching here is for us:
     * accept it and let the FreeRTOS+TCP IP task do the real per-endpoint routing. (Previously this
     * returned 0 for anything not matching our IPv4 address/broadcast/multicast, which silently
     * dropped all IPv6 - SLAAC RAs never arrived and the device was unreachable over IPv6.) */
    ( void ) ulDest;
    ( void ) ulOurIP;
    ( void ) ulMask;
    return 1;
}

/* kind: 0=IP 1=GW 2=MASK. Returns a pointer to a 4-byte (network-order) address;
 * cached per-kind so the three can be held at once. */
unsigned char * amebad_tcp_get_addr( int idx, int kind )
{
    static uint32_t ulAddrCache[ 3 ];
    NetworkEndPoint_t * pxEP;
    ( void ) idx;

    if( ( kind < 0 ) || ( kind > 2 ) )
    {
        return NULL;
    }
    pxEP = ( pxMyInterface != NULL ) ? FreeRTOS_FirstEndPoint( pxMyInterface ) : NULL;
    if( pxEP == NULL )
    {
        ulAddrCache[ kind ] = 0;
    }
    else if( kind == 0 )
    {
        ulAddrCache[ kind ] = pxEP->ipv4_settings.ulIPAddress;
    }
    else if( kind == 1 )
    {
        ulAddrCache[ kind ] = pxEP->ipv4_settings.ulGatewayAddress;
    }
    else
    {
        ulAddrCache[ kind ] = pxEP->ipv4_settings.ulNetMask;
    }
    return ( unsigned char * ) &ulAddrCache[ kind ];
}

#if ( ipconfigUSE_IPv6 != 0 )

/* lwIP-API shim the SDK's wifi_conf.c calls on connect (netif_create_ip6_linklocal_address(&xnetif[0],
 * 1)) as its IPv6 bring-up. Under FreeRTOS+TCP this registers two endpoints on our interface:
 *   - the fe80::(EUI-64) link-local, derived from the 48-bit MAC (the RA process uses it as the
 *     Router-Solicitation source - see FreeRTOS_RA.c xGetLinkLocalAddress), and
 *   - a global endpoint with bWantRA, which autoconfigures its address + gateway from the Router
 *     Advertisement (stateless address autoconfiguration / SLAAC).
 * The MAC is taken from the already-filled IPv4 endpoint, so the opaque lwIP `netif` arg is unused.
 * Idempotent (registers the pair once); the RA/DAD starts on the next network-up - the bring-up's
 * post-connect FreeRTOS_NetworkDown() re-kick processes the new endpoints. */
void amebad_ipv6_init( void )
{
    static NetworkEndPoint_t xEndPoint6_LinkLocal;
    static NetworkEndPoint_t xEndPoint6_Global;
    static BaseType_t        xDone = pdFALSE;
    NetworkEndPoint_t      * pxIPv4;
    IPv6_Address_t           xLinkLocal, xLLPrefix, xGlobal, xGlobalPrefix, xGateway;
    const uint8_t          * mac;

    if( ( xDone != pdFALSE ) || ( pxMyInterface == NULL ) )
    {
        return;
    }
    pxIPv4 = FreeRTOS_FirstEndPoint( pxMyInterface );
    if( pxIPv4 == NULL )
    {
        return;
    }
    mac = pxIPv4->xMACAddress.ucBytes;

    /* fe80::(EUI-64): insert ff:fe in the middle of the 48-bit MAC, flip the U/L bit. */
    ( void ) memset( xLinkLocal.ucBytes, 0, ipSIZE_OF_IPv6_ADDRESS );
    xLinkLocal.ucBytes[ 0 ]  = 0xfeU;
    xLinkLocal.ucBytes[ 1 ]  = 0x80U;
    xLinkLocal.ucBytes[ 8 ]  = ( uint8_t ) ( mac[ 0 ] ^ 0x02U );
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
    FreeRTOS_FillEndPoint_IPv6( pxMyInterface, &xEndPoint6_LinkLocal, &xLinkLocal, &xLLPrefix,
                                10U, NULL, NULL, mac );

    /* Global endpoint: address + prefix + gateway are learned from the Router Advertisement. */
    ( void ) memset( xGlobal.ucBytes, 0, ipSIZE_OF_IPv6_ADDRESS );
    ( void ) memset( xGlobalPrefix.ucBytes, 0, ipSIZE_OF_IPv6_ADDRESS );
    ( void ) memset( xGateway.ucBytes, 0, ipSIZE_OF_IPv6_ADDRESS );
    FreeRTOS_FillEndPoint_IPv6( pxMyInterface, &xEndPoint6_Global, &xGlobal, &xGlobalPrefix,
                                64U, &xGateway, NULL, mac );
    xEndPoint6_Global.bits.bWantRA = pdTRUE_UNSIGNED;   /* SLAAC */

    xDone = pdTRUE;
}

/* SDK lwIP-API name: wifi_conf.c calls netif_create_ip6_linklocal_address(&xnetif[0], 1) from wifi_on.
 * That runs before pxRealtek_FillInterfaceDescriptor(), so amebad_ipv6_init() early-returns there; the
 * bring-up calls amebad_ipv6_init() again once the interface exists. Forwarding keeps the SDK call
 * site working (and harmless) without dereferencing the opaque lwIP netif. */
struct netif;
void netif_create_ip6_linklocal_address( struct netif * netif, unsigned char from_mac_48bit )
{
    ( void ) netif;
    ( void ) from_mac_48bit;
    amebad_ipv6_init();
}

/* NB: the Realtek driver does NOT support multicast-MAC registration - wifi_register_multicast_address()
 * returns -95 (ENOTSUP, "ioctl[RTKIOCDEVPRIVATE] error"). So IPv6 multicast RX (unsolicited RAs + the
 * router's solicited-node Neighbor Solicitations) cannot be opened from here; the device forms its SLAAC
 * address from the unicast solicited RA but stays unreachable inbound over IPv6. Left unimplemented on
 * purpose - re-adding the registration only spams the error. */

#endif /* ( ipconfigUSE_IPv6 != 0 ) */

/* ===== Application hooks the stack requires ================================ */

/* Simple xorshift RNG — adequate for DHCP xid in a bring-up (not crypto). */
static uint32_t prvRand32( void )
{
    static uint32_t ulSeed = 0x9e3779b9u;
    ulSeed ^= ( uint32_t ) xTaskGetTickCount() + 0x1234567u;
    ulSeed ^= ulSeed << 13;
    ulSeed ^= ulSeed >> 17;
    ulSeed ^= ulSeed << 5;
    return ulSeed;
}

BaseType_t xApplicationGetRandomNumber( uint32_t * pulNumber )
{
    *pulNumber = prvRand32();
    return pdTRUE;
}

uint32_t ulApplicationGetNextSequenceNumber( uint32_t ulSourceAddress,
                                             uint16_t usSourcePort,
                                             uint32_t ulDestinationAddress,
                                             uint16_t usDestinationPort )
{
    ( void ) ulSourceAddress;
    ( void ) usSourcePort;
    ( void ) ulDestinationAddress;
    ( void ) usDestinationPort;
    return prvRand32();
}

const char * pcApplicationHostnameHook( void )
{
    return "amebad";
}
