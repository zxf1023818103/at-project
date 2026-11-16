#include "ameba_soc.h"
#include "wifi_api.h"
#include "wifi_api_ext.h"
#include "at_netif.h"
#include "wifi_service.h"
#include "wifi_port.h"
#ifdef AT_ENABLE_NAN
#include "../nan/nan_network.h"
#endif
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
extern void wifi_set_rom2flash(void);
extern void whc_ipc_host_init(void);
struct eth_drv_sg;
extern int whc_ipc_host_send(int, struct eth_drv_sg *, int, int, void *, uint8_t);
static uint8_t sta_mac[6];
static volatile int connected, enabled, connecting;
int amebagreen2_wifi_ready(void) { return enabled; }
static uint32_t ip, mask, gw;
static uint32_t notified_ip;
static void mac_get(uint8_t out[6]) { memcpy(out, sta_mac, 6); }
void at_netif_wifi_mac(uint8_t out[6]) { mac_get(out); }
void at_netif_bt_mac(uint8_t out[6])
{
    /* SDK hci_platform.c uses logical OTP 0x1b4..0x1b9 in display order. */
    if (OTP_LogicalRead(out, 0x1b4, 6) != RTK_SUCCESS ||
        !memcmp(out, "\xff\xff\xff\xff\xff\xff", 6)) {
        mac_get(out);
        out[0] = (out[0] | 2) & ~1; /* deterministic locally administered fallback */
    }
}
const char *at_netif_platform_name(void) { return "amebagreen2"; }
static BaseType_t link_up(void) { return connected ? pdTRUE : pdFALSE; }
static BaseType_t random_word(uint32_t *out) { return TRNG_get_random_bytes(out, 4) == 0 ? pdTRUE : pdFALSE; }
static BaseType_t transmit(const uint8_t *data, uint16_t len)
{
    struct { unsigned int buf, len; } sg = {(unsigned int)data, len};
    return whc_ipc_host_send(0, (struct eth_drv_sg *)&sg, 1, len, NULL, 0) == 0 ? pdTRUE : pdFALSE;
}
static void got_ip(uint32_t address)
{
    if (address && connected && address != notified_ip) {
        notified_ip = address;
        wifi_service_on_event(WIFI_SVC_EV_GOTIP);
        at_netif_on_network_up();
    }
}
#if ipconfigIPv4_BACKWARD_COMPATIBLE == 0
void vApplicationIPNetworkEventHook_Multi(eIPCallbackEvent_t event, NetworkEndPoint_t *ep)
{
    if (ep->pxNetworkInterface != at_netif_get() || ep->bits.bIPv6) return;
#else
void vApplicationIPNetworkEventHook(eIPCallbackEvent_t event)
{
#endif
    if (event == eNetworkUp) {
        uint32_t address = 0;
        FreeRTOS_GetEndPointConfiguration(&address, NULL, NULL, NULL, FreeRTOS_FirstEndPoint(at_netif_get()));
        got_ip(address);
    } else if (notified_ip) {
        notified_ip = 0;
        at_netif_on_network_down();
    }
}
static const at_netif_ops_t ops = {
    .tx = transmit, .link_up = link_up, .mac_get = mac_get,
    .get_random = random_word, .on_got_ip = got_ip
#ifdef AT_ENABLE_NAN
    , .add_interfaces = green2_nan_net_init, .exclusive_interfaces = pdTRUE
#endif
};
void amebagreen2_rx(const void *data, size_t len)
{
    NetworkBufferDescriptor_t *buf = at_netif_rx_alloc(len);
    if (!buf) return;
    memcpy(buf->pucEthernetBuffer, data, len);
    at_netif_rx_submit(buf, len);
}
void amebagreen2_join_event(uint8_t status)
{
    int was_connected = connected;
    connected = status == RTW_JOINSTATUS_SUCCESS;
    if (connected) { wifi_service_on_event(WIFI_SVC_EV_CONNECTED); if (at_netif_is_ready()) at_netif_kick(); }
    else if (was_connected) {
        if (at_netif_is_ready()) at_netif_kick();
        if (!connecting) wifi_service_on_event(WIFI_SVC_EV_DISCONNECT);
    }
}
uint8_t *LwIP_GetMAC(uint8_t idx) {
#ifdef AT_ENABLE_NAN
    if (idx == 2) return green2_nan_mac();
#endif
    return sta_mac;
}
static void addresses(void)
{
    if (at_netif_is_ready()) {
        NetworkEndPoint_t *ep = FreeRTOS_FirstEndPoint(at_netif_get());
        if (ep && !ep->bits.bIPv6) FreeRTOS_GetEndPointConfiguration(&ip, &mask, &gw, NULL, ep);
    }
}
unsigned char *LwIP_GetIP(uint8_t idx) { (void)idx; addresses(); return (unsigned char *)&ip; }
unsigned char *LwIP_GetGW(uint8_t idx) { (void)idx; addresses(); return (unsigned char *)&gw; }
unsigned char *LwIP_GetMASK(uint8_t idx) { (void)idx; addresses(); return (unsigned char *)&mask; }
void LwIP_wlan_set_netif_info(uint8_t idx, void *dev, unsigned char *addr) {
    (void)dev;
    if (!idx) memcpy(sta_mac, addr, 6);
#ifdef AT_ENABLE_NAN
    if (idx == 2) green2_nan_mac_set(addr);
#endif
}
void LwIP_netif_set_up(uint8_t idx) { (void)idx; }
void LwIP_netif_set_down(uint8_t idx) { (void)idx; connected = 0; }
void LwIP_netif_set_link_up(uint8_t idx) { if (!idx) amebagreen2_join_event(RTW_JOINSTATUS_SUCCESS); }
void LwIP_netif_set_link_down(uint8_t idx) { if (!idx) amebagreen2_join_event(RTW_JOINSTATUS_DISCONNECT); }
int LwIP_netif_is_valid_IP(uint8_t idx, unsigned char *dest) { (void)idx; (void)dest; return 1; }
unsigned wifi_port_task_stack_words(void) { return 4096; }
int wifi_port_enable(void)
{
    wifi_set_rom2flash();
#ifdef AT_ENABLE_NAN
    extern void wifi_set_task_size(void);
    wifi_set_task_size();
#endif
    whc_ipc_host_init();
    int rc = wifi_on(RTW_MODE_STA);
    if (rc) return rc;
    extern void amebagreen2_country_init(void);
    amebagreen2_country_init();
    struct rtw_mac mac;
    if (wifi_get_mac_address(0, &mac, 0) == 0) memcpy(sta_mac, &mac, 6);
    at_netif_set_ops(&ops); at_netif_bringup();
    enabled = 1;
    printf("[WiFi] ready %02x:%02x:%02x:%02x:%02x:%02x\n", sta_mac[0],sta_mac[1],sta_mac[2],sta_mac[3],sta_mac[4],sta_mac[5]);
    return 0;
}
void wifi_port_autoreconnect_disable(void) {}
int wifi_port_scan(const char *ssid, uint8_t *chan, int *sec)
{
    struct rtw_scan_param param = {0};
    struct rtw_scan_result *result = pvPortMalloc(8 * sizeof(*result));
    if (!result) return -1;
    uint32_t count = 8;
    param.ssid = (uint8_t *)ssid; param.max_ap_record_num = count;
    int rc = wifi_scan_networks(&param, 1);
    if (rc < 0 || wifi_get_scan_records(&count, result) < 0) { vPortFree(result); return -1; }
    int found = -1;
    for (uint32_t i = 0; i < count; ++i) {
        if (result[i].ssid.len == strlen(ssid) && !memcmp(result[i].ssid.val, ssid, result[i].ssid.len)) {
            *chan = result[i].channel; *sec = result[i].security; found = 0; break;
        }
    }
    printf("[WiFi] scan rc=%d records=%lu matched=%d\n", rc, (unsigned long)count, found == 0);
    vPortFree(result);
    return found;
}
int wifi_port_connect(const char *ssid, const char *pass, uint8_t chan, int sec)
{
    /* A WPS handoff can already have an authenticated connection. Rejoining it
     * emits a disconnect during the synchronous connect and causes a retry loop. */
    if (connected) {
        struct rtw_wifi_setting current = {0};
        if (!wifi_get_setting(0, &current) && !strcmp((const char *)current.ssid, ssid)) return 0;
    }
    struct rtw_network_info info = {0};
    size_t n = strlen(ssid);
    if (n > sizeof(info.ssid.val)-1) return -1;
    info.ssid.len = n; memcpy(info.ssid.val, ssid, n);
    info.password = (uint8_t *)pass; info.password_len = strlen(pass);
    info.channel = chan; info.security_type = sec < 0 ? (info.password_len ? RTW_SECURITY_WPA2_AES_PSK : RTW_SECURITY_OPEN) : (uint32_t)sec;
    connecting = 1;
    int rc = wifi_connect(&info, 1);
    connecting = 0;
    return rc == 0 && !connected ? -1 : rc;
}
void wifi_port_disconnect(void) { wifi_disconnect(); }
int wifi_port_sec_from_str(const char *s) { return s && *s ? (int)strtoul(s,NULL,0) : -1; }
const char *wifi_port_sec_to_str(int sec) { static char buf[16]; snprintf(buf,sizeof(buf),"%u",(unsigned)sec); return buf; }
void wifi_port_reset_chip(void) { System_Reset(); for (;;) {} }

#ifdef AT_ENABLE_NAN
unsigned char *lwip_get_ip(uint8_t idx) { return LwIP_GetIP(idx); }
unsigned char *lwip_get_gw(uint8_t idx) { return LwIP_GetGW(idx); }
unsigned char *lwip_get_mask(uint8_t idx) { return LwIP_GetMASK(idx); }
unsigned char *lwip_get_mac(uint8_t idx) { return LwIP_GetMAC(idx); }
int lwip_is_valid_ip(uint8_t idx, unsigned char *dest) { return LwIP_netif_is_valid_IP(idx, dest); }
int lwip_get_ipv6_enabled(void) { return 1; }
void lwip_wlan_set_netif_info(uint8_t idx, void *dev, unsigned char *mac) { LwIP_wlan_set_netif_info(idx, dev, mac); }
#endif
