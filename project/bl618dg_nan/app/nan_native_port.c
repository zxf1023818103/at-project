/* Radio adapter only: NAN/PASN, TCP, service and benchmark code stay shared. */
#include "whc_usb.h"
#include "nan_platform.h"
#include "nan_network.h"
#include "whc_ipc_host_nan.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

static void (*event_callback)(uint16_t, void *, uint32_t);
static void (*reply_callback)(void *, uint32_t);
static uint8_t macs[3][6];
static volatile unsigned mac_valid;
extern void bl618dg_nan_network_start(void);
extern void bl618dg_nan_store_init(void);

static void receive(const struct whc_frame *f)
{
    const uint8_t *p = f->data;
    if (f->event == WHC_RX_PACKET) {
        if (f->code == 2) green2_nan_rx(p, f->length);
    } else if (f->event == WHC_API_CALL) {
        if (f->code == 0x4003 && f->length >= 8) {
            uint32_t id = whc_get32(p), size = whc_get32(p + 4);
            if (id <= UINT16_MAX && size <= f->length - 8 && event_callback)
                event_callback(id, (void *)(p + 8), size);
        } else if (f->code == 0x4004 && f->length >= 4) {
            uint32_t size = whc_get32(p);
            if (size <= f->length - 4 && reply_callback) reply_callback((void *)(p + 4), size);
        } else if (f->code == 9 && f->length == 10 && whc_get32(p) < 3) {
            unsigned index = whc_get32(p);
            memcpy(macs[index], p + 4, 6); mac_valid |= 1u << index;
        }
    } else if (f->event == WHC_COMMAND && f->length >= 12 &&
               whc_get32(p) == 0xffa5a5a5 && p[4] == 1 && p[5] < 3) {
        memcpy(macs[p[5]], p + 6, 6); mac_valid |= 1u << p[5];
    }
}
void bl618dg_native_init(uintptr_t base)
{
    bl618dg_nan_store_init();
    whc_usb_init(base, receive);
}
void whc_host_nan_register_evt_cb(void (*cb)(uint16_t, void *, uint32_t)) { event_callback = cb; }
void whc_host_nan_register_cmd_reply_cb(void (*cb)(void *, uint32_t)) { reply_callback = cb; }
int whc_host_nan_init(void) { return whc_usb_call(0x4001, NULL, 0); }
int whc_host_nan_deinit(void) { return whc_usb_call(0x4002, NULL, 0); }
int whc_host_nan_stop(void) { return whc_usb_call(0x4004, NULL, 0); }
int green2_nan_radio_ready(void) { return whc_usb_connected(); }
int green2_nan_radio_start(void)
{
    const uint32_t params[] = {112, 3}; /* Same master preference / 2.4+5 GHz. */
    return whc_usb_call(0x4003, params, sizeof(params));
}
int whc_host_nan_cfgvendor_cmd(uint16_t id, const void *data, uint32_t length)
{
    if (length > WHC_USB_OUT_MTU - WHC_HEADER_SIZE - 8 || (length && !data)) return -1;
    uint8_t *p = pvPortMalloc(8 + length);
    if (!p) return -1;
    whc_put32(p, id); whc_put32(p + 4, length);
    if (length) memcpy(p + 8, data, length);
    int rc = whc_usb_call(0x4007, p, length + 8);
    volatile uint8_t *wipe = p;
    for (size_t i = 0; i < length + 8; ++i) wipe[i] = 0;
    vPortFree(p);
    return rc;
}
int green2_nan_vendor(unsigned short id, const void *data, size_t size)
{ return whc_host_nan_cfgvendor_cmd(id, data, size); }
int green2_nan_mgmt(const unsigned char *data, size_t size)
{
    if (!data || size > WHC_USB_OUT_MTU - WHC_HEADER_SIZE - 12) return -1;
    uint8_t *p = pvPortMalloc(size + 12);
    if (!p) return -1;
    whc_put32(p, 2); whc_put32(p + 4, size); whc_put32(p + 8, 0);
    memcpy(p + 12, data, size);
    int rc = whc_usb_call(0x101b, p, size + 12);
    memset(p, 0, size + 12); vPortFree(p);
    return rc;
}
int green2_nan_read_mac(void)
{
    /* Ask for the NAN interface, not a guessed derivation of the STA MAC. */
    const uint8_t cmd[] = {0xa5, 0xa5, 0xa5, 0xff, 1, 2};
    mac_valid &= ~(1u << 2);
    if (whc_usb_send(WHC_COMMAND, 0, cmd, sizeof(cmd))) return -1;
    for (unsigned i = 0; i < 200 && !(mac_valid & (1u << 2)); ++i) vTaskDelay(pdMS_TO_TICKS(10));
    if (!(mac_valid & (1u << 2))) { printf("[WHC-USB] NAN MAC query timeout\n"); return -1; }
    green2_nan_mac_set(macs[2]);
    printf("[NAN] native radio NMI %02x:%02x:%02x:%02x:%02x:%02x\n",
        macs[2][0],macs[2][1],macs[2][2],macs[2][3],macs[2][4],macs[2][5]);
    return 0;
}
int green2_nan_send(const void *data, size_t size)
{ return whc_usb_send(WHC_TX_PACKET, 2, data, size); }
int green2_nan_radio_enable(void)
{
    for (unsigned i = 0; i < 600 && !whc_usb_connected(); ++i) vTaskDelay(pdMS_TO_TICKS(50));
    if (!whc_usb_connected()) return -1;
    /* Keep the original cfg80211=1: the firmware gates NAN callbacks on it.
     * WHC_API_WIFI_SET_HOST_RTOS would incorrectly disable those callbacks. */
    uint32_t mode = 1;
    int rc = whc_usb_call(9, &mode, sizeof(mode));
    printf("[WHC-USB] original firmware Wi-Fi on rc=%d\n", rc);
    if (rc) return rc;
    bl618dg_nan_network_start();
    return 0;
}
