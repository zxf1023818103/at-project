/*
 * FreeRTOS-Plus-TCP <-> Bouffalo bl60x (BL602) wifi4 driver bridge. The shared
 * NetworkInterface boilerplate (descriptor, prv*, RX-submit, bring-up, stack
 * callbacks) lives in component/freertos-plus-tcp_config/at_netif.c; this file is
 * just the BL602 driver glue (ops + TX/RX entry points the driver calls by name).
 *
 * Data path:
 *   TX  IP task -> at_netif prvOutput -> bl602_ops.tx -> bl602_ll_output() [bl_tx.c]
 *   RX  driver tcpip_stack_input() [bl_utils.c] -> bl602_tcp_rx_alloc/submit [here]
 *       -> at_netif_rx_submit -> xSendEventStructToIPTask(eNetworkRxEvent)
 * BL602 only.
 */
#include "FreeRTOS.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Routing.h"
#include "at_netif.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bflb_sec_trng.h"    /* bflb_trng_readlen — HW TRNG for the DHCP xid / TCP ISN */

extern int  platform_post_event( int type, int code, int code2 );
extern int  wifi_mgmr_sta_mac_get( uint8_t mac[ 6 ] );
extern int  bl602_ll_output( int is_sta, void * frame, uint16_t len, void * cfm );

/* EV_WIFI / CODE_WIFI_ON_GOT_IP (must match wifi_mgmr_ext.h:19 so the app's
 * wifi_event_handler routes it instead of printing "Unknown code"). */
#ifndef EV_WIFI
#define EV_WIFI                 1
#endif
#ifndef CODE_WIFI_ON_GOT_IP
#define CODE_WIFI_ON_GOT_IP     7
#endif

/* ===== platform ops ======================================================== */
static BaseType_t prvTx( const uint8_t * frame, uint16_t len )
{
    /* bl602_ll_output copies into a firmware TX block; is_sta = 1 (STA-only). */
    return ( bl602_ll_output( 1, ( void * ) frame, len, NULL ) == 0 ) ? pdTRUE : pdFALSE;
}

static BaseType_t prvLinkUp( void )
{
    /* wifi_mgmr_state_get(): 0x03 = CONNECTED_IP_GETTING (DHCP can run), 0x04 = IP_GOT. */
    extern int wifi_mgmr_state_get( int * state );
    int state = 0;
    wifi_mgmr_state_get( &state );
    return ( ( state == 0x03 ) || ( state == 0x04 ) ) ? pdTRUE : pdFALSE;
}

static void prvMacGet( uint8_t mac[ 6 ] )
{
    ( void ) wifi_mgmr_sta_mac_get( mac );
}

static BaseType_t prvGetRandom( uint32_t * out )
{
    return ( bflb_trng_readlen( ( uint8_t * ) out, sizeof( *out ) ) == 0 ) ? pdTRUE : pdFALSE;
}

static const char * prvHostname( void )
{
    static char host[ 24 ] = { 0 };
    if( host[ 0 ] == '\0' )
    {
        uint8_t mac[ 6 ] = { 0 };
        ( void ) wifi_mgmr_sta_mac_get( mac );
        ( void ) snprintf( host, sizeof( host ), "bl602_%02x%02x%02x", mac[ 3 ], mac[ 4 ], mac[ 5 ] );
    }
    return host;
}

static void prvOnGotIP( uint32_t ulIP )
{
    ( void ) ulIP;
    /* Advance wifi_mgmr out of wifiConnected_ipObtaining (FreeRTOS+TCP owns DHCP, so
     * the mgmr never sees lwIP's got-IP callback and would self-disconnect otherwise). */
    extern int wifi_mgmr_api_ip_got( void );
    wifi_mgmr_api_ip_got();
    platform_post_event( EV_WIFI, CODE_WIFI_ON_GOT_IP, 0 );  /* tell the app an IP is up */
}

static const at_netif_ops_t bl602_ops =
{
    .tx         = prvTx,
    .link_up    = prvLinkUp,
    .running    = NULL,           /* driver up before the IP task -> always initialised */
    .mac_get    = prvMacGet,
    .get_random = prvGetRandom,
    .hostname   = prvHostname,
    .on_got_ip  = prvOnGotIP,
};

/* ===== RX entry points (called from bl_utils.c tcpip_stack_input) ========== *
 * Driver assembles the frame into the NetworkBuffer from rx_alloc, then submits. */
NetworkBufferDescriptor_t * bl602_tcp_rx_alloc( uint32_t len )
{
    return at_netif_rx_alloc( ( size_t ) len );
}

void bl602_tcp_rx_submit( NetworkBufferDescriptor_t * pxBuf, uint32_t len )
{
    at_netif_rx_submit( pxBuf, ( size_t ) len );
}

/* ===== app-facing API (unchanged names) ==================================== */
void bl602_freertos_tcp_set_ready( void ) { at_netif_set_ready(); }

void bl602_tcp_bringup( void )
{
    at_netif_set_ops( &bl602_ops );
    at_netif_bringup();
}

/* Re-kick DHCP — called from bl_rx.c on the STA connect indication. */
void bl602_tcp_kick( void ) { at_netif_kick(); }

/* Live IPv4 config from the STA endpoint (all network-order). */
int bl602_tcp_ip4_get( uint32_t * addr, uint32_t * mask, uint32_t * gw, uint32_t * dns )
{
    NetworkEndPoint_t * px = FreeRTOS_FirstEndPoint( NULL );

    if( addr != NULL ) { *addr = ( px != NULL ) ? px->ipv4_settings.ulIPAddress      : 0U; }
    if( mask != NULL ) { *mask = ( px != NULL ) ? px->ipv4_settings.ulNetMask        : 0U; }
    if( gw   != NULL ) { *gw   = ( px != NULL ) ? px->ipv4_settings.ulGatewayAddress : 0U; }
    if( dns  != NULL ) { *dns  = ( px != NULL ) ? px->ipv4_settings.ulDNSServerAddresses[ 0 ] : 0U; }
    return 0;
}
