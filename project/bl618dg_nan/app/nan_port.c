#include "nan_usb.h"
#include "nan_platform.h"
#include "nan_network.h"
#include "whc_ipc_host_nan.h"
#include "rtw_nan_vendor_def.h"
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "NetworkBufferManagement.h"
#include "at_netif.h"
#include "bflb_sec_trng.h"
#include "mm.h"
#include "mbedtls/ecp.h"
#include <stdio.h>
#include <string.h>
static void (*event_callback)(uint16_t, void *, uint32_t);
static void (*reply_callback)(void *, uint32_t);
static BaseType_t network_ready;
void bl618dg_nan_receive(const struct nan_usb_record *r, const void *data)
{
    if (r->type == NAN_USB_EVENT && event_callback) event_callback(r->code, (void *)data, r->length);
    if (r->type == NAN_USB_COMMAND_REPLY && reply_callback) reply_callback((void *)data, r->length);
    if (r->type == NAN_USB_ETHERNET) green2_nan_rx(data, r->length);
}
void whc_host_nan_register_evt_cb(void (*cb)(uint16_t, void *, uint32_t)) { event_callback = cb; }
void whc_host_nan_register_cmd_reply_cb(void (*cb)(void *, uint32_t)) { reply_callback = cb; }
static int command(unsigned id) { return nan_usb_rpc(id, NULL, 0, NULL, NULL); }
int whc_host_nan_init(void) { return command(NAN_USB_INIT); }
int whc_host_nan_deinit(void) { return command(NAN_USB_DEINIT); }
int whc_host_nan_stop(void) { return command(NAN_USB_STOP); }
int green2_nan_radio_start(void) { return command(NAN_USB_START); }
int green2_nan_radio_ready(void) { return nan_usb_connected(); }
int whc_host_nan_cfgvendor_cmd(uint16_t id, const void *data, uint32_t length)
{
    if (length > NAN_USB_MAX_PAYLOAD - 4) return -1;
    uint8_t *p = pvPortMalloc(length + 4);
    if (!p) return -1;
    uint32_t code = id; memcpy(p, &code, 4);
    if (length) memcpy(p + 4, data, length);
    int rc = nan_usb_rpc(NAN_USB_VENDOR, p, length + 4, NULL, NULL);
    volatile uint8_t *wipe = p;
    for (size_t i = 0; i < length + 4; ++i) wipe[i] = 0;
    vPortFree(p); return rc;
}
int green2_nan_vendor(unsigned short id, const void *data, size_t size)
{ return whc_host_nan_cfgvendor_cmd(id, data, size); }
int green2_nan_mgmt(const unsigned char *data, size_t size)
{ return nan_usb_rpc(NAN_USB_MGMT, data, size, NULL, NULL); }
int green2_nan_store(void *data, size_t size, int write)
{
    if (write) return nan_usb_rpc(NAN_USB_STORE_WRITE, data, size, NULL, NULL);
    uint32_t requested = size; size_t received = size;
    int rc = nan_usb_rpc(NAN_USB_STORE_READ, &requested, 4, data, &received);
    return rc ? rc : (received == size ? 0 : -1);
}
int green2_nan_read_mac(void)
{
    uint8_t mac[6]; size_t size = sizeof(mac);
    int rc = nan_usb_rpc(NAN_USB_MAC, NULL, 0, mac, &size);
    if (!rc && size == sizeof(mac)) green2_nan_mac_set(mac); else return -1;
    printf("[NAN] radio NMI %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    return 0;
}
int green2_nan_send(const void *data, size_t size)
{ return nan_usb_send(NAN_USB_ETHERNET, 0, 0, 0, data, size); }
int green2_nan_random(void *data, size_t size) { return bflb_trng_readlen(data, size); }
void green2_nan_heap_check(const char *where) { (void)where; }
void green2_nan_p256_init(void) { printf("[NAN] P-256 mbedTLS software backend on BL618DG AP\n"); }
int green2_nan_p256_mul(mbedtls_ecp_group *g, mbedtls_ecp_point *r, const mbedtls_mpi *k, const mbedtls_ecp_point *p)
{ (void)g; (void)r; (void)k; (void)p; return MBEDTLS_ERR_ECP_FEATURE_UNAVAILABLE; }
int mbedtls_hardware_poll(void *ctx, unsigned char *out, size_t size, size_t *used)
{ (void)ctx; int rc = green2_nan_random(out, size); *used = rc ? 0 : size; return rc; }
BaseType_t xApplicationGetRandomNumber(uint32_t *out) { return green2_nan_random(out, 4) ? pdFALSE : pdTRUE; }
uint32_t ulApplicationGetNextSequenceNumber(uint32_t a, uint16_t b, uint32_t c, uint16_t d)
{ (void)a; (void)b; (void)c; (void)d; uint32_t n; configASSERT(xApplicationGetRandomNumber(&n)); return n; }
const char *pcApplicationHostnameHook(void) { return "bl618dg-nan"; }
BaseType_t xApplicationDNSQueryHook_Multi(NetworkEndPoint_t *ep, const char *name)
{ (void)ep; return name && !strcmp(name, "bl618dg-nan") ? pdTRUE : pdFALSE; }
size_t xPortGetFreeHeapSize(void)
{ return kfree_size(MM_HEAP_PSRAM_0) + kfree_size(MM_HEAP_OCRAM_0) + kfree_size(MM_HEAP_WRAM_0); }
void vApplicationIPNetworkEventHook_Multi(eIPCallbackEvent_t event, NetworkEndPoint_t *ep)
{
    (void)ep;
    static BaseType_t was_up;
    if (event == eNetworkUp || was_up)
        printf("[NAN] AP IPv6 network %s\n", event == eNetworkUp ? "UP" : "DOWN");
    was_up = event == eNetworkUp;
}
BaseType_t at_netif_is_ready(void) { return network_ready; }
NetworkBufferDescriptor_t *at_netif_rx_alloc(size_t length) { return pxGetNetworkBufferWithDescriptor(length, 0); }

int green2_nan_radio_enable(void)
{
    for (unsigned i = 0; i < 600 && !nan_usb_connected(); ++i) vTaskDelay(pdMS_TO_TICKS(50));
    if (!nan_usb_connected()) return -1;
    uint32_t abi[5]; size_t size = sizeof(abi);
    const uint32_t expected[] = {NAN_USB_VERSION, sizeof(struct rtw_nan_publish_data),
        sizeof(struct rtw_nan_datapath_request_received_event_data),
        sizeof(struct rtw_nan_datapath_response), sizeof(struct rtw_nan_datapath_established_event_data)};
    if (nan_usb_rpc(NAN_USB_HELLO, NULL, 0, abi, &size) || size != sizeof(abi) || memcmp(abi, expected, size)) {
        printf("[NAN-USB] radio ABI/readiness check failed\n"); return -1;
    }
    /* Exercise both USB directions and every record size before publication. */
    uint8_t *sent = pvPortMalloc(NAN_USB_MAX_PAYLOAD), *got = pvPortMalloc(NAN_USB_MAX_PAYLOAD);
    configASSERT(sent && got);
    int rc = 0;
    const size_t lengths[] = {1, 511, 512, 1514, 2048, NAN_USB_MAX_PAYLOAD};
    for (unsigned r = 0; r < 12 && !rc; ++r) {
        size_t length = lengths[r % 6];
        for (size_t i = 0; i < length; ++i) sent[i] = (uint8_t)(i * 37 + r);
        size = NAN_USB_MAX_PAYLOAD;
        rc = nan_usb_rpc(NAN_USB_ECHO, sent, length, got, &size);
        if (!rc && (size != length || memcmp(sent, got, length))) rc = -1;
    }
    vPortFree(sent); vPortFree(got);
    printf("[NAN-USB] ABI + 12 payload roundtrips %s\n", rc ? "FAIL" : "PASS");
    if (rc) return rc;
    if (!network_ready) {
        green2_nan_net_init();
        configASSERT(FreeRTOS_IPInit_Multi() == pdPASS);
        network_ready = pdTRUE;
    }
    return 0;
}
