#ifndef __WIFI_NETIF_H__
#define __WIFI_NETIF_H__
/* lwIP removed: DHCP is automatic via the FreeRTOS-Plus-TCP endpoint (bWantDHCP);
 * these are now no-ops kept for the wifi_mgmr call sites. */
int wifi_netif_dhcp_start(void);
int wifi_netif_dhcp_stop(void);
#endif
