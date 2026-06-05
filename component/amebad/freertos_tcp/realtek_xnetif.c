/*
 * xnetif[] storage for the Realtek WiFi driver glue.
 *
 * The vendored Realtek port (component/amebad/wlan/src/lwip_intf.c) references a
 * global `struct netif xnetif[NET_IF_NUM]` and accesses its fields (hwaddr, ...),
 * so the array must be a real lwip `struct netif` — this is the ONLY lwip-typed
 * dependency we keep when running FreeRTOS+TCP instead of lwip. Normally
 * lwip_netconf.c defines it; for the FreeRTOS+TCP build lwip_netconf.c is not
 * compiled (it also drags in LwIP_Init / the lwip IP stack), so the array is
 * defined here in isolation. lwip_intf.c uses it purely as a per-VIF handle
 * (netif_get_idx) and to stash the MAC; no lwip IP processing runs on it.
 */
#include "lwip_netconf.h"    /* lwip-free: vendored struct netif + NET_IF_NUM */

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
