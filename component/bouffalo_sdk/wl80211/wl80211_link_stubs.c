/*
 * wl80211_link_stubs.c - weak no-op stubs for the lwIP-era link / AP-DHCP hooks.
 *
 * wifi_mgmr.c's deferred tasks (connected_tsk / disconnect_tsk / start_dhcpd_tsk / stop_dhcpd_tsk) call
 * _wifi_mgmr_sta_link_up/down + _wifi_mgmr_ap_start/stop_dhcpd, which were defined in wl80211/lwip.c.
 * lwIP was removed on Bouffalo (the network stack is FreeRTOS-Plus-TCP), so lwip.c is not compiled and
 * those symbols are undefined. The FreeRTOS-Plus-TCP STA link-up -> DHCP is driven by the netif
 * (component/bouffalo_sdk/freertos_tcp_port/bouffalo_netif.c), NOT this legacy path, so no-op is correct
 * here. These are __weak so a real FreeRTOS-Plus-TCP re-implementation (a future BL616 bring-up step)
 * can override them.
 *
 * NB: this only affects BL616/BL616CL (wl80211). bl602 (wifi4) never used this path. Previously the
 * whole chain was dead-code-stripped because BL616's wifi_mgmr_init is not yet wired from the app; the
 * bflb_async_event -> direct-dispatch change made wl80211_on_driver_event reachable via the driver's
 * wl80211_post_event, which is what surfaced these (pre-existing) missing lwIP symbols.
 */
#include <stdint.h>
#include <stdbool.h>

__attribute__((weak)) void _wifi_mgmr_sta_link_up(int use_dhcp)
{
    (void) use_dhcp;
}

__attribute__((weak)) void _wifi_mgmr_sta_link_down(void)
{
}

__attribute__((weak)) void _wifi_mgmr_ap_start_dhcpd(bool use_ipcfg, bool use_dhcpd, int start, int limit,
                                                     uint32_t ap_ipaddr, uint32_t ap_mask)
{
    (void) use_ipcfg; (void) use_dhcpd; (void) start; (void) limit; (void) ap_ipaddr; (void) ap_mask;
}

__attribute__((weak)) void _wifi_mgmr_ap_stop_dhcpd(void)
{
}

/* wifi_mgmr_event_handler (the wl80211 internal CODE_WIFI_ON_* handler) calls this on GOT_IP to print
 * the lwIP netif's IP/GW/mask. lwIP is gone (FreeRTOS-Plus-TCP owns addressing and prints [NET] on
 * eNetworkUp via at_netif_on_network_up), so a no-op is correct. Surfaced once BL616's wifi_mgmr_init
 * got wired from the app -- same lwIP-era origin as the stubs above. */
__attribute__((weak)) void _wifi_mgmr_ip_got_dump(void)
{
}

/* wl80211_init() (prebuilt macsw.c) calls wl80211_lwip_init() to attach the lwIP netif + register the
 * RX input callback. lwIP is gone; the FreeRTOS-Plus-TCP netif (bouffalo_netif.c) owns the RX up-call
 * (wl80211_tcpip_input) and TX, registering its own input cb. So a no-op (return 0) is correct here --
 * it only lets wl80211_init link (wl80211_init itself is what initializes wl80211_glb: the scan_ops
 * STAILQ + command environment, without which the first scan faults). __weak so the tcp port can
 * override. */
__attribute__((weak)) int wl80211_lwip_init(void)
{
    return 0;
}
