/* FHOST net_al implementation for FreeRTOS+TCP. No lwIP objects or APIs. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_IP_Private.h"
#include "NetworkBufferManagement.h"
#include "bouffalo_wifi_tcp.h"
#include "net_al.h"
#include "net_al_ext.h"
#include "fhost.h"
#include "fhost_api.h"
#include "wifi_mgmr.h"
#include "wifi_mgmr_ext.h"
#include "eloop_rtos.h"
#include "eloop.h"
#include "mm.h"

#define WIFI_INTERFACES 2
struct wifi_interface {
    NetworkInterface_t interface;
    NetworkEndPoint_t endpoint;
    void *vif;
    uint8_t mac[6];
    uint16_t l2_filter;
    volatile bool up;
    bool ip_up;
    char name[4];
};
/* TX descriptors are opaque to FHOST; DMA payload and headroom live in WRAM. */
struct wifi_tx {
    uint16_t length;
    uint16_t split; /* RX forwarding ABI requires separate Ethernet/payload segments. */
    uint8_t headroom[NET_AL_TX_HEADROOM];
    uint8_t data[];
};
static struct wifi_interface interfaces[WIFI_INTERFACES];
static volatile unsigned interface_count;
static bool ip_started;
static unsigned rx_packets, tx_packets, rx_drops, tx_drops;
static SemaphoreHandle_t l2_lock, l2_done;
static bool l2_ack;
static bool exposed(struct wifi_interface *wifi)
{
#ifdef CONFIG_PAN_SOFT_AP
    return wifi==&interfaces[MGMR_VIF_AP];
#else
    return wifi==&interfaces[MGMR_VIF_STA];
#endif
}
extern int fhost_tx_start(net_al_if_t, net_al_tx_t, cb_fhost_tx, void *);
extern int fhost_tx_req_do(net_al_if_t, net_al_tx_t, int, void *, void *, void *, int);
extern void fhost_rx_buf_push(void *);
extern void fhost_rx_do_resend_req(void *, net_al_tx_t, bool);
extern void platform_post_event(int, int, int);

static BaseType_t initialise(NetworkInterface_t *interface)
{ return ((struct wifi_interface *)interface->pvArgument)->up ? pdPASS : pdFAIL; }
static BaseType_t link_status(NetworkInterface_t *interface)
{ return ((struct wifi_interface *)interface->pvArgument)->up; }
static BaseType_t output(NetworkInterface_t *interface, NetworkBufferDescriptor_t *buffer, BaseType_t release)
{
    struct wifi_interface *wifi = interface->pvArgument;
    BaseType_t result = pdFAIL;
    if (wifi->up && buffer->xDataLength >= 14 && buffer->xDataLength <= 1514) {
        struct wifi_tx *tx = net_buf_tx_alloc_fill(buffer->pucEthernetBuffer, buffer->xDataLength);
        if (tx) {
            /* On success FHOST consumes the buffer; queue failure leaves it owned here. */
            if (fhost_tx_start(wifi, tx, NULL, NULL) == 0) {
                ++tx_packets;
                result = pdPASS;
            } else net_buf_tx_free(tx);
        }
    }
    if (result != pdPASS) ++tx_drops;
    if (release) vReleaseNetworkBufferAndDescriptor(buffer);
    return result;
}
int net_init(void)
{
    l2_lock = xSemaphoreCreateMutex();
    l2_done = xSemaphoreCreateBinary();
    return l2_lock && l2_done ? 0 : -1;
}
int net_if_add(net_al_if_t *out, const uint8_t *mac, const uint32_t *ip,
               const uint32_t *mask, const uint32_t *gateway, void *vif)
{
    (void)ip; (void)mask; (void)gateway;
    if (!out || !mac || interface_count >= WIFI_INTERFACES || ip_started) return -1;
    unsigned index = interface_count;
    struct wifi_interface *wifi = &interfaces[index];
    wifi->vif = vif;
    memcpy(wifi->mac, mac, 6);
    snprintf(wifi->name, sizeof(wifi->name), "wl%u", index + 1);
    wifi->interface.pcName = wifi->name;
    wifi->interface.pvArgument = wifi;
    wifi->interface.pfInitialise = initialise;
    wifi->interface.pfOutput = output;
    wifi->interface.pfGetPhyLinkStatus = link_status;
    /* FHOST needs both opaque VIF handles; expose only the selected role.
     * A dormant AP endpoint with 0.0.0.0/0 would match every off-subnet
     * destination in FreeRTOS+TCP's route lookup, shadowing the STA gateway. */
    if (exposed(wifi)) {
        FreeRTOS_AddNetworkInterface(&wifi->interface);
        const uint8_t zero[4] = {0};
#ifdef CONFIG_PAN_SOFT_AP
        const uint8_t address[4]={192,168,77,1}, netmask[4]={255,255,255,0};
        FreeRTOS_FillEndPoint(&wifi->interface,&wifi->endpoint,address,netmask,zero,zero,mac);
#else
        const uint8_t host_mask[4] = {255, 255, 255, 255};
        FreeRTOS_FillEndPoint(&wifi->interface, &wifi->endpoint, zero, host_mask, zero, zero, mac);
        wifi->endpoint.bits.bWantDHCP = pdTRUE;
#endif
    }
    *out = wifi;
    ++interface_count;
    printf("[wifi] registered %s\r\n", wifi->name);
    return 0;
}
BaseType_t bouffalo_wifi_tcp_interfaces_ready(void)
{ return interface_count == WIFI_INTERFACES ? pdPASS : pdFAIL; }
void bouffalo_wifi_tcp_started(void) { ip_started = true; }
const uint8_t *net_if_get_mac_addr(net_al_if_t net_if)
{ return ((struct wifi_interface *)net_if)->mac; }
net_al_if_t net_if_find_from_name(const char *name)
{
    for (unsigned i = 0; i < interface_count; ++i)
        if (!strcmp(name, interfaces[i].name)) return &interfaces[i];
    return NULL;
}
int net_if_get_name(net_al_if_t net_if, char *buffer, int length)
{
    if (!net_if || !buffer || length <= 0) return -1;
    return snprintf(buffer, length, "%s", ((struct wifi_interface *)net_if)->name);
}
void *net_if_vif_info(net_al_if_t net_if)
{ return ((struct wifi_interface *)net_if)->vif; }
void net_if_up_cb(net_al_if_t net_if)
{
    /* FHOST's administrative-up precedes association. Link/DHCP events
     * activate the FreeRTOS endpoint only after association succeeds. */
    (void)net_if;
}
void net_if_down_cb(net_al_if_t net_if)
{
    struct wifi_interface *wifi = net_if;
    wifi->up = false;
    if (ip_started && exposed(wifi)) FreeRTOS_NetworkDown(&wifi->interface);
}
void net_al_input(net_al_rx_t buffer, void *payload, net_al_if_t net_if,
                  uint16_t length, uint8_t offset, uint8_t skip, net_buf_free_fn free_fn)
{
    struct wifi_interface *wifi = net_if;
    if (!wifi || length < skip + 14 || length - skip > 1514) { ++rx_drops; goto done; }
    if (skip) memmove((uint8_t *)payload + offset + skip, (uint8_t *)payload + offset, 14);
    uint8_t *frame = (uint8_t *)payload + offset + skip;
    unsigned size = length - skip;
    uint16_t type = (uint16_t)frame[12] << 8 | frame[13];
    if (wifi->l2_filter && type == wifi->l2_filter) {
        int id = eloop_get_l2_event_id(wifi->name);
        if (eloop_event_commit(id, (const char *)frame, size)) ++rx_drops;
        goto done;
    }
    if (!wifi->up || !ip_started) { ++rx_drops; goto done; }
    NetworkBufferDescriptor_t *packet = pxGetNetworkBufferWithDescriptor(size, 0);
    if (!packet) { ++rx_drops; goto done; }
    memcpy(packet->pucEthernetBuffer, frame, size);
    packet->xDataLength = size;
    packet->pxInterface = &wifi->interface;
    packet->pxEndPoint = &wifi->endpoint;
    IPStackEvent_t event = {.eEventType = eNetworkRxEvent, .pvData = packet};
    if (xSendEventStructToIPTask(&event, 0) != pdPASS) {
        vReleaseNetworkBufferAndDescriptor(packet);
        ++rx_drops;
    } else ++rx_packets;
done:
    free_fn(buffer);
}
net_al_tx_t net_buf_tx_alloc(uint32_t length)
{
    if (!length || length > 4096) return NULL;
    struct wifi_tx *tx = kmalloc(sizeof(*tx) + length, MM_FLAG_HEAP_WRAM_0 | MM_FLAG_PROP_ZERO);
    if (tx) tx->length = length;
    return tx;
}
net_al_tx_t net_buf_tx_alloc_fill(const uint8_t *frame, uint32_t length)
{
    struct wifi_tx *tx = net_buf_tx_alloc(length);
    if (tx) memcpy(tx->data, frame, length);
    return tx;
}
void *net_buf_tx_info(net_al_tx_t *buffer, uint16_t *length, int *segments,
                      uint32_t addresses[], uint16_t lengths[], uint32_t *headroom)
{
    struct wifi_tx *tx = (struct wifi_tx *)buffer;
    configASSERT(*segments >= 1);
    *length = tx->length;
    unsigned count=tx->split ? 2 : 1;
    configASSERT(*segments >= (int)count);
    *segments = count;
    addresses[0] = (uint32_t)tx->data;
    lengths[0] = tx->split ? tx->split : tx->length;
    if(tx->split) {
        addresses[1]=(uint32_t)(tx->data+tx->split);
        lengths[1]=tx->length-tx->split;
    }
    /* Match FHOST ABI: signed displacement from payload back to headroom. */
    if (headroom) *headroom = (uint32_t)(tx->headroom - tx->data);
    return tx->headroom;
}
bool net_buf_tx_all_shram(net_al_tx_t buffer) { (void)buffer; return true; }
void net_buf_tx_free(net_al_tx_t buffer) { kfree(buffer); }
int net_al_tx_req(struct net_al_tx_req req)
{ return fhost_tx_req_do(req.net_if, req.net_buf, req.type, req.cfm_cb, req.cfm_cb_arg, req.buf_rx, req.no_cck); }
void net_al_tx_cfm(void) {}
void net_al_tx_do_sta_del(uint8_t sta_id, release_buf_cb release_buf)
{ (void)sta_id; (void)release_buf; }
void net_al_tx_init(void) {}
void net_al_rx_resend(bool forward, void *buffer, void *payload, int length, int offset, int header)
{
    struct wifi_tx *tx = header>=14 && header<length ?
        net_buf_tx_alloc_fill((uint8_t *)payload + offset, length) : NULL;
    if (tx) {
        tx->split=header;
        fhost_rx_do_resend_req(buffer, tx, forward);
    }
    else if (!forward) fhost_rx_buf_push(buffer);
}
static void l2_complete(uint32_t frame_id, bool acknowledged, void *arg)
{
    (void)frame_id; (void)arg;
    l2_ack = acknowledged;
    xSemaphoreGive(l2_done);
}
int net_l2_send(net_al_if_t net_if, const uint8_t *data, int length, uint16_t type,
                const uint8_t *destination, bool *ack)
{
    struct wifi_interface *wifi = net_if;
    if (!wifi || !data || length < 0 || length > 1500 || !ack) return -1;
    struct wifi_tx *tx = net_buf_tx_alloc(length + (destination ? 14 : 0));
    if (!tx) return -1;
    uint8_t *frame = tx->data;
    if (destination) {
        memcpy(frame, destination, 6); memcpy(frame + 6, wifi->mac, 6);
        frame[12] = type >> 8; frame[13] = type; frame += 14;
    }
    memcpy(frame, data, length);
    xSemaphoreTake(l2_lock, portMAX_DELAY);
    l2_ack = false;
    int result = fhost_tx_start(wifi, tx, l2_complete, NULL);
    /* Callback lifetime stays valid even for delayed MAC confirmation. */
    if (result == 0) xSemaphoreTake(l2_done, portMAX_DELAY);
    else net_buf_tx_free(tx);
    *ack = l2_ack;
    xSemaphoreGive(l2_lock);
    return result;
}
int net_l2_socket_create(net_al_if_t net_if, uint16_t type)
{
    struct wifi_interface *wifi = net_if;
    if (!wifi || wifi->l2_filter) return -1;
    wifi->l2_filter = type; return 0;
}
int net_l2_socket_delete(net_al_if_t net_if)
{ if (!net_if) return -1; ((struct wifi_interface *)net_if)->l2_filter = 0; return 0; }
uint16_t net_ip_chksum(const void *data, int length)
{
    const uint8_t *bytes = data;
    uint32_t sum = 0;
    while (length > 1) { sum += (uint16_t)bytes[0] | (uint16_t)bytes[1] << 8; bytes += 2; length -= 2; }
    if (length) sum += *bytes;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return sum; /* FHOST expects the unfolded lwip_standard_chksum convention. */
}
int net_al_link_set(net_al_if_t net_if)
{
    if (!exposed(net_if)) return -1;
    struct wifi_interface *wifi = net_if;
    wifi->up = true;
    if (ip_started && exposed(wifi)) FreeRTOS_NetworkDown(&wifi->interface);
    return 0;
}
int net_al_ext_dhcp_connect_vif(int index, int api, uint32_t timeout)
{
    (void)api; (void)timeout;
    if (index != MGMR_VIF_STA || index >= interface_count) return -1;
    return net_al_link_set(&interfaces[index]);
}
int net_al_ext_dhcp_connect(int api, uint32_t timeout)
{ return net_al_ext_dhcp_connect_vif(MGMR_VIF_STA, api, timeout); }
void net_al_ext_dhcp_disconnect_vif(int index)
{ if (index >= 0 && index < interface_count) net_if_down_cb(&interfaces[index]); }
void net_al_ext_dhcp_disconnect(void) { net_al_ext_dhcp_disconnect_vif(MGMR_VIF_STA); }
int net_al_ext_get_vif_ip(int index, struct net_al_ext_ip_addr_cfg *cfg)
{
    if (index < 0 || index >= interface_count || !cfg) return -1;
    NetworkEndPoint_t *endpoint = &interfaces[index].endpoint;
    memset(cfg, 0, sizeof(*cfg));
    cfg->mode = index==MGMR_VIF_AP ? IP_ADDR_STATIC_IPV4 : IP_ADDR_DHCP_CLIENT;
    cfg->ipv4.addr = endpoint->ipv4_settings.ulIPAddress;
    cfg->ipv4.mask = endpoint->ipv4_settings.ulNetMask;
    cfg->ipv4.gw = endpoint->ipv4_settings.ulGatewayAddress;
    cfg->ipv4.dns = endpoint->ipv4_settings.ulDNSServerAddresses[0];
    return 0;
}
int net_al_ext_set_vif_ip(int index, struct net_al_ext_ip_addr_cfg *cfg)
{
#ifdef CONFIG_PAN_SOFT_AP
    if(cfg && index==MGMR_VIF_AP && cfg->mode==IP_ADDR_STATIC_IPV4) {
        /* Fixed isolated test subnet, registered before IPInit. */
        if(cfg->ipv4.addr!=FreeRTOS_inet_addr("192.168.77.1") ||
           cfg->ipv4.mask!=FreeRTOS_inet_addr("255.255.255.0")) return -1;
        return net_al_link_set(&interfaces[index]);
    }
#endif
    if (!cfg || index != MGMR_VIF_STA || cfg->mode != IP_ADDR_DHCP_CLIENT) return -1;
    return net_al_ext_dhcp_connect_vif(index, 1, cfg->dhcp.to_ms);
}
/* DHCP server support is restricted to the opt-in SoftAP test image. */
int net_al_dhcpd_start(net_al_if_t interface, int start, int limit)
{
#ifdef CONFIG_PAN_SOFT_AP
    extern int wifi_test_dhcp_start(int,int);
    if(interface!=&interfaces[MGMR_VIF_AP]) return -1;
    return wifi_test_dhcp_start(start,limit);
#else
    (void)interface; (void)start; (void)limit; return -1;
#endif
}
int net_al_dhcpd_stop(net_al_if_t interface)
{
#ifdef CONFIG_PAN_SOFT_AP
    extern void wifi_test_dhcp_stop(void);
    if(interface!=&interfaces[MGMR_VIF_AP]) return -1;
    wifi_test_dhcp_stop();return 0;
#else
    (void)interface; return -1;
#endif
}
int net_al_set_ipv6_enable(int enable) { return enable ? -1 : 0; }
void bouffalo_wifi_tcp_network_event(eIPCallbackEvent_t event, NetworkEndPoint_t *endpoint)
{
    for (unsigned i = 0; i < interface_count; ++i) {
        if (endpoint != &interfaces[i].endpoint) continue;
        bool up = event == eNetworkUp;
        if (!up && !interfaces[i].ip_up) return;
        interfaces[i].ip_up = up;
        char address[16]; FreeRTOS_inet_ntoa(endpoint->ipv4_settings.ulIPAddress, address);
        printf("[wifi] %s IP %s %s\r\n", interfaces[i].name, event == eNetworkUp ? "UP" : "DOWN", address);
        if (i == MGMR_VIF_STA) platform_post_event(EV_WIFI, event == eNetworkUp ? CODE_WIFI_ON_GOT_IP : CODE_WIFI_ON_LOST_IP, 0);
    }
}
void bouffalo_wifi_tcp_status(void)
{
    printf("[wifi] interfaces=%u rx=%u tx=%u rx_drop=%u tx_drop=%u\r\n", interface_count, rx_packets, tx_packets, rx_drops, tx_drops);
    for (unsigned i = 0; i < interface_count; ++i) {
        if (!exposed(&interfaces[i])) continue;
        char ip[16], gateway[16], dns[16];
        NetworkEndPoint_t *endpoint = &interfaces[i].endpoint;
        FreeRTOS_inet_ntoa(endpoint->ipv4_settings.ulIPAddress, ip);
        FreeRTOS_inet_ntoa(endpoint->ipv4_settings.ulGatewayAddress, gateway);
        FreeRTOS_inet_ntoa(endpoint->ipv4_settings.ulDNSServerAddresses[0], dns);
        printf("[wifi] %s link=%u IP=%s gateway=%s dns=%s\r\n", interfaces[i].name,
               interfaces[i].up, ip, gateway, dns);
    }
}
