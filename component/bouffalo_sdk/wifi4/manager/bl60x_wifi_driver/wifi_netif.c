
#include "wifi_mgmr.h"
#include "wifi_netif.h"

#define DEBUG_HEADER "[WF][IF] "

/* lwIP removed: DHCP is driven by the FreeRTOS-Plus-TCP endpoint (bWantDHCP) and
 * (re)kicked from the driver connect indication (bl602_tcp_kick). These wrappers
 * are no-ops kept so the wifi_mgmr state machine call sites still link. */
int wifi_netif_dhcp_start(void)
{
    return 0;
}

int wifi_netif_dhcp_stop(void)
{
    return 0;
}
