/*
 * FreeRTOS-Plus-TCP <-> Realtek AmebaD WiFi driver bridge. The shared
 * NetworkInterface boilerplate (descriptor, prv*, RX-submit, stack callbacks) lives
 * in component/freertos-plus-tcp_config/at_netif.c; this file is the Realtek driver
 * glue: the RX up-call (pull model + drain), the driver RX-filter/addr getters
 * lwip_intf.c needs, IPv6 SLAAC, and the ops. The bring-up itself + the IPv6/MLDv2
 * network-event hook stay in app_km4/main.c (it keeps its own interface pointer for
 * the reconnect FreeRTOS_NetworkDown()).
 *
 * Data path:
 *   TX  IP task -> at_netif prvOutput -> ops.tx -> rltk_wlan_send()
 *   RX  driver -> netif_rx(idx,len) [lwip_intf.c] -> amebad_tcp_netif_input() [here]
 *       -> rltk_wlan_recv() copy -> at_netif_rx_submit -> xSendEventStructToIPTask
 */
#include "FreeRTOS.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Routing.h"
#include "at_netif.h"
#include <stdlib.h>                   /* getenv (HOSTNAME) */
#include <stdio.h>                    /* snprintf (hostname default) */
#include <string.h>

/* ---- Realtek driver glue (mirror of component/amebad/wlan/osdep/lwip_intf.h) -- */
struct eth_drv_sg { unsigned int buf; unsigned int len; };
extern int  rltk_wlan_send( int idx, struct eth_drv_sg * sg_list, int sg_len, int total_len );
extern void rltk_wlan_recv( int idx, struct eth_drv_sg * sg_list, int sg_len );
extern unsigned char rltk_wlan_running( unsigned char idx );
extern int  wifi_is_connected_to_ap( void );

#define WLAN_STA_IDX    0

/* Force-pull our larger TX/RX skb pool (rtw_opt_skbuf.o) over lib_wlan.a's WEAK 8/10
 * defaults (else effective TX = 6 buffers -> the high-rate upload wedge). See rtw_opt_skbuf.c. */
extern int rtw_opt_skbuf_force_link;
__attribute__( ( used ) ) static int * const amebad_pull_skbuf = &rtw_opt_skbuf_force_link;

/* ===== platform ops ======================================================== */
static BaseType_t prvTx( const uint8_t * frame, uint16_t len )
{
    struct eth_drv_sg sg;
    if( rltk_wlan_running( WLAN_STA_IDX ) == 0 )
    {
        return pdFALSE;
    }
    sg.buf = ( unsigned int ) frame;
    sg.len = ( unsigned int ) len;
    /* rltk_wlan_send copies out synchronously; the descriptor can be freed after. */
    return ( rltk_wlan_send( WLAN_STA_IDX, &sg, 1, ( int ) len ) == 0 ) ? pdTRUE : pdFALSE;
}

static BaseType_t prvLinkUp( void )  { return ( wifi_is_connected_to_ap() == 0 ) ? pdTRUE : pdFALSE; } /* 0 == connected */
static BaseType_t prvRunning( void ) { return ( rltk_wlan_running( WLAN_STA_IDX ) != 0 ) ? pdTRUE : pdFALSE; }

/* HW RNG: the boot-ROM Rand() (component/amebad/string_rom/rand.h). */
extern unsigned int Rand( void );
static BaseType_t prvGetRandom( uint32_t * out ) { *out = ( uint32_t ) Rand(); return pdTRUE; }

/* MAC for the default hostname: efuse BT/BLE MAC via TF-M (AWS/STORAGE images); efuse_logical_read
 * is weak so non-TF-M images fall back to the interface (WiFi) MAC. */
static void prvHostMac( uint8_t mac[ 6 ] )
{
    extern int efuse_logical_read( uint16_t addr, uint16_t size, uint8_t * pbuf ) __attribute__( ( weak ) );
    if( ( efuse_logical_read != NULL ) && ( efuse_logical_read( 0x190U, 6U, mac ) == 0 ) )   /* 0x190 = BT/BLE MAC */
    {
        return;
    }
    NetworkEndPoint_t * px = FreeRTOS_FirstEndPoint( NULL );
    if( px != NULL ) { ( void ) memcpy( mac, px->xMACAddress.ucBytes, 6 ); }
}

/* Hostname for mDNS/DHCP: HOSTNAME env (KV-backed) wins; else "amebad_<BLE-MAC last 3>". */
static const char * prvHostname( void )
{
    return at_netif_hostname( "amebad", prvHostMac );   /* HOSTNAME override else amebad_<mac> */
}

/* mac_get / on_got_ip are NULL: app_km4/main.c owns the bring-up (its own interface
 * pointer for reconnect) + the IPv6/MLDv2 network-event hook (strong override of the
 * weak at_netif one). */
static const at_netif_ops_t realtek_ops =
{
    .tx         = prvTx,
    .link_up    = prvLinkUp,
    .running    = prvRunning,
    .mac_get    = NULL,
    .get_random = prvGetRandom,
    .hostname   = prvHostname,
    .on_got_ip  = NULL,
};

/* Fill + register the interface descriptor. Called from main.c before IPInit; also
 * registers the ops (so the shared prv* / stack callbacks have them). Keeps the
 * pxRealtek_* name so main.c's call site is unchanged. */
NetworkInterface_t * pxRealtek_FillInterfaceDescriptor( BaseType_t xEMACIndex,
                                                        NetworkInterface_t * pxInterface )
{
    at_netif_set_ops( &realtek_ops );
    return at_netif_fill_descriptor( xEMACIndex, pxInterface );
}

void amebad_freertos_tcp_set_ready( void ) { at_netif_set_ready(); }

/* ===== RX bridge (pull model + always-drain) =============================== *
 * The driver delivers a frame via netif_rx(idx,len) [lwip_intf.c] -> here. We copy it
 * out of the driver (rltk_wlan_recv) into a NetworkBuffer. MUST always drain (even when
 * dropping) or the driver's RX ring stalls. */
void amebad_tcp_netif_input( int idx, unsigned int len )
{
    NetworkBufferDescriptor_t * pxBuf;
    struct eth_drv_sg sg;
    int total_len = ( int ) len;

    if( ( total_len <= 0 ) || ( total_len > ( int ) ( ipconfigNETWORK_MTU + 14 + 4 ) ) )
    {
        return;
    }

    pxBuf = at_netif_rx_alloc( ( size_t ) total_len );   /* NULL if stack not ready OR pool exhausted */
    if( pxBuf == NULL )
    {
        static unsigned char ucSink[ ipconfigNETWORK_MTU + 18 ] __attribute__( ( aligned( 4 ) ) );
        sg.buf = ( unsigned int ) ucSink;
        sg.len = ( unsigned int ) total_len;
        rltk_wlan_recv( idx, &sg, 1 );                   /* drain (else RX ring stalls) */
        return;
    }

    sg.buf = ( unsigned int ) pxBuf->pucEthernetBuffer;
    sg.len = ( unsigned int ) total_len;
    rltk_wlan_recv( idx, &sg, 1 );
    at_netif_rx_submit( pxBuf, ( size_t ) total_len );
}

/* ===== Driver RX filter + address getters (called from lwip_intf.c) ======== */
int amebad_tcp_is_valid_ip( int idx, const unsigned char * ip_dest )
{
    ( void ) idx;
    ( void ) ip_dest;
    /* The driver's MAC-level RX filter already gates frames; accept everything here and
     * let the FreeRTOS+TCP IP task do the per-endpoint routing (dropping here silently
     * killed all IPv6 - SLAAC RAs never arrived). */
    return 1;
}

/* kind: 0=IP 1=GW 2=MASK. Returns a pointer to a 4-byte (network-order) address. */
unsigned char * amebad_tcp_get_addr( int idx, int kind )
{
    static uint32_t ulAddrCache[ 3 ];
    NetworkInterface_t * pxIf = at_netif_get();
    NetworkEndPoint_t  * pxEP;
    ( void ) idx;

    if( ( kind < 0 ) || ( kind > 2 ) )
    {
        return NULL;
    }
    pxEP = ( pxIf != NULL ) ? FreeRTOS_FirstEndPoint( pxIf ) : NULL;
    if( pxEP == NULL )                { ulAddrCache[ kind ] = 0; }
    else if( kind == 0 )             { ulAddrCache[ kind ] = pxEP->ipv4_settings.ulIPAddress; }
    else if( kind == 1 )             { ulAddrCache[ kind ] = pxEP->ipv4_settings.ulGatewayAddress; }
    else                             { ulAddrCache[ kind ] = pxEP->ipv4_settings.ulNetMask; }
    return ( unsigned char * ) &ulAddrCache[ kind ];
}

#if ( ipconfigUSE_IPv6 != 0 )

/* AmebaD name kept for its two callers (main.cpp bring-up + the lwIP-API wifi_on hook
 * below); the endpoint-filling impl is now shared in at_netif_ipv6_init()
 * (component/freertos-plus-tcp_config/at_netif.c) so bl602 and AmebaD run identical code. */
void amebad_ipv6_init( void )
{
    extern void at_netif_ipv6_init( void );
    at_netif_ipv6_init();
}

/* SDK lwIP-API name: wifi_conf.c calls this from wifi_on (before the interface exists,
 * so amebad_ipv6_init early-returns; the bring-up calls it again once it does). */
struct netif;
void netif_create_ip6_linklocal_address( struct netif * netif, unsigned char from_mac_48bit )
{
    ( void ) netif;
    ( void ) from_mac_48bit;
    amebad_ipv6_init();
}

#endif /* ( ipconfigUSE_IPv6 != 0 ) */
