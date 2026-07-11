/*
 * at_netif.h — the cross-platform FreeRTOS-Plus-TCP STA NetworkInterface helper.
 *
 * The boilerplate every platform's WiFi<->FreeRTOS+TCP bridge used to duplicate
 * (realtek_netif.c / bl602_netif.c / bouffalo_netif.c): the NetworkInterface
 * descriptor + prvInitialise/prvGetPhyLinkStatus/prvOutput, the DHCP IPv4 bring-up,
 * the RX-submit tail, and the single-per-image stack callbacks (random / hostname /
 * DNS-query). The per-platform WiFi driver is threaded in via a small ops struct;
 * each platform keeps only its driver TX/RX + any extras (amebad IPv6/filters/xnetif).
 *
 * Boot order: at_netif_set_ops(&ops)  ->  at_netif_bringup()  (both once, before/at
 * the point the WiFi STA driver is up). The RX path calls at_netif_rx_alloc/submit.
 */
#ifndef AT_NETIF_H
#define AT_NETIF_H

#include "FreeRTOS.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Routing.h"
#include "NetworkBufferManagement.h"
#include <stdint.h>
#include <stddef.h>

/* Per-platform WiFi-driver hooks the shared helper calls. Registered once via
 * at_netif_set_ops() before at_netif_bringup(). Unset (NULL) optional fields default
 * as noted. */
typedef struct
{
    /* Send one linear ethernet frame to the driver; return pdTRUE on success.
     * (amebad rltk_wlan_send / bl602 bl602_ll_output / bl616 wl80211 mac_tx.) */
    BaseType_t ( * tx )( const uint8_t * frame, uint16_t len );
    /* pdTRUE when the STA is associated (phy link up). */
    BaseType_t ( * link_up )( void );
    /* pdTRUE when the driver is running (pfInitialise). NULL -> always pdTRUE. */
    BaseType_t ( * running )( void );
    /* Read the STA MAC into mac[6]. */
    void ( * mac_get )( uint8_t mac[ 6 ] );
    /* HW random for the DHCP xid / TCP ISN; return pdTRUE on success. */
    BaseType_t ( * get_random )( uint32_t * out );
    /* Hostname for mDNS/DHCP (returns a static string). */
    const char * ( * hostname )( void );
    /* Called on eNetworkUp with the leased IPv4 (advance driver state / post app
     * event). NULL -> nothing extra. */
    void ( * on_got_ip )( uint32_t ulIPAddress );
} at_netif_ops_t;

/* Register the platform ops (call once, before at_netif_bringup). */
void at_netif_set_ops( const at_netif_ops_t * ops );

/* Shared fallback hostname for a platform's ops.hostname: HOSTNAME env override if set, else
 * "<platform_name>_<mac[3..5]>". mac_get fills mac[6], called at most once (builds the default). */
const char * at_netif_hostname( const char * platform_name, void ( * mac_get )( uint8_t mac[ 6 ] ) );

/* Fill + register the STA interface descriptor (shared prv* over the ops). Stores it
 * for RX tagging; returns the interface. idx = WLAN VIF index (0 = STA). */
NetworkInterface_t * at_netif_fill_descriptor( BaseType_t xEMACIndex, NetworkInterface_t * pxInterface );

/* One-shot STA bring-up: fill descriptor + a DHCP IPv4 endpoint + FreeRTOS_IPInit_Multi
 * + set_ready. Idempotent (uses internal static interface/endpoint storage). */
void at_netif_bringup( void );

/* RX: platform allocs a buffer (rx_alloc), fills it with the frame, then submits it
 * (rx_submit tags interface+endpoint, routes, posts eNetworkRxEvent; releases on drop).
 * rx_alloc returns NULL if the stack isn't ready yet or len is out of range. */
NetworkBufferDescriptor_t * at_netif_rx_alloc( size_t len );
void at_netif_rx_submit( NetworkBufferDescriptor_t * pxBuf, size_t len );

void       at_netif_set_ready( void );      /* mark the stack up (IPInit done) */
BaseType_t at_netif_is_ready( void );
NetworkInterface_t * at_netif_get( void );   /* the registered interface (NULL before bringup) */
void       at_netif_kick( void );            /* re-kick DHCP (FreeRTOS_NetworkDown) */

/* Shared network-up/down body. Each platform's strong vApplicationIPNetworkEventHook does its own
 * dedup + driver signalling, then calls these: they print the DHCP config uniformly ("[NET]") +
 * apply the DNS_SERVER/DNS_SERVER2 env override, and _up() fans out to app_on_network_up(). */
void at_netif_on_network_up( void );
void at_netif_on_network_down( void );
/* IPv6 logging + MLDv2-snooping interop (shared; #if ipconfigUSE_IPv6, else no-op stubs):
 *  - at_netif_log_ipv6: print each configured IPv6 address (called by at_netif_on_network_up).
 *  - at_netif_ipv6_global_set: 1 once the SLAAC global address has formed.
 *  - at_netif_ipv6_mld_start: start the periodic MLDv2 reporter (idempotent) so an MLD-snooping
 *    bridge forwards inbound IPv6 (NS/RA/mDNS). Called from at_netif_bringup + AmebaD's bring-up. */
void at_netif_log_ipv6( const char * tag );
int  at_netif_ipv6_global_set( void );
void at_netif_ipv6_mld_start( void );

#endif /* AT_NETIF_H */
