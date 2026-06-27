/*
 * xnetif[] storage for the Realtek WiFi driver glue.
 *
 * The prebuilt driver (lib_wlan.a) and the vendored wifi_conf.c reference a global
 * `struct netif xnetif[NET_IF_NUM]` and access its fields (hwaddr@+50,
 * hwaddr_len@+56) on a 72-byte stride. lwIP is removed — `struct netif` is the
 * layout-asserted vendored shim from netif_shim.h (the irreducible driver ABI),
 * NOT lwIP. The array is defined here; amebad_register_wlan_netif() stashes the
 * MAC and LwIP_GetMAC() returns it (load-bearing on the connect-path mgmt-frame
 * filter). No IP processing runs on it — the IP stack is FreeRTOS-Plus-TCP.
 */
#include "netif_shim.h"    /* lwip-free: vendored struct netif + NET_IF_NUM */

struct netif xnetif[ NET_IF_NUM ];

extern void rltk_wlan_set_netif_info( int idx_wlan, void * dev, unsigned char * dev_addr );

/* Register a WLAN VIF's netif handle + MAC with the driver, so the driver
 * delivers RX (via netif_rx -> ethernetif_recv) and knows the interface MAC.
 * Replaces what lwip's netif_add/low_level_init did, minus the lwip IP stack.
 * Keeps the lwip-typed xnetif[] access contained to this file. */
void amebad_register_wlan_netif( int idx, unsigned char * mac )
{
    int i;
    for( i = 0; i < 6; i++ )
    {
        xnetif[ idx ].hwaddr[ i ] = mac[ i ];
    }
    xnetif[ idx ].hwaddr_len = 6;
    rltk_wlan_set_netif_info( idx, ( void * ) &xnetif[ idx ], mac );
}

/* lwip address getters that the Realtek WiFi driver/glue calls (e.g. the mgmt-
 * frame handler does memcmp(frame_DA, LwIP_GetMAC(&xnetif[0]), 6) to tell if a
 * frame is for this STA). These MUST return real pointers — a NULL MAC faults
 * the connect path. The MAC comes from xnetif[].hwaddr (set above); IP/GW/MASK
 * are not tracked in xnetif under FreeRTOS+TCP, so return zeroed statics (only
 * referenced on status/report paths, value not load-bearing for bring-up). */
unsigned char * LwIP_GetMAC( struct netif * pnetif )  { return pnetif->hwaddr; }

static unsigned char ucZeroAddr[ 4 ] = { 0, 0, 0, 0 };
unsigned char * LwIP_GetIP( struct netif * pnetif )   { ( void ) pnetif; return ucZeroAddr; }
unsigned char * LwIP_GetGW( struct netif * pnetif )   { ( void ) pnetif; return ucZeroAddr; }
unsigned char * LwIP_GetMASK( struct netif * pnetif ) { ( void ) pnetif; return ucZeroAddr; }
