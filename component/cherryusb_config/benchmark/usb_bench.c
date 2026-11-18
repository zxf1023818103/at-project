/* Shared CherryUSB Bulk benchmark. USB control and console traffic are not timed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "usbd_core.h"
#include "usbh_core.h"
#include "usb_bench.h"

#define BENCH_MAGIC 0x42535543u
#define BENCH_START 0x5au
#define BENCH_STATUS 0x5bu
#define BENCH_MAX_BLOCK 16384u
#define BENCH_IN 0x84u
#define BENCH_OUT 0x05u

struct bench_status {
    uint32_t magic, direction, total, block, bytes, packets, bad_words, io_errors;
};
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint32_t data_buffer[BENCH_MAX_BLOCK / 4];
static struct bench_status state;
static unsigned verify_data;

static uint32_t pattern(unsigned i) { return 0x9e3779b9u * i ^ 0x56a93c17u; }
static void prepare_data(unsigned bytes)
{
    for (unsigned i = 4; i < bytes / 4; ++i) data_buffer[i] = pattern(i);
}
static void set_header(uint32_t offset, unsigned bytes)
{
    data_buffer[0] = BENCH_MAGIC;
    data_buffer[1] = offset / bytes;
    data_buffer[2] = offset;
    data_buffer[3] = bytes;
}
static unsigned check_data(uint32_t offset, unsigned bytes, unsigned full)
{
    unsigned bad = (data_buffer[0] != BENCH_MAGIC) + (data_buffer[1] != offset / bytes)
        + (data_buffer[2] != offset) + (data_buffer[3] != bytes);
    if (full) for (unsigned i = 4; i < bytes / 4; ++i) bad += data_buffer[i] != pattern(i);
    return bad;
}

#if USB_BENCH_HOST
static struct usbh_hubport *volatile peer;
static struct usb_endpoint_descriptor *ep_in, *ep_out;
/* Each endpoint keeps its own DATA0/DATA1 state across transfers. */
static struct usbh_urb bulk_urbs[2];
static TaskHandle_t worker;
static volatile unsigned busy;
static unsigned run_mib = 64, run_block = BENCH_MAX_BLOCK, run_verify = 1, run_rounds = 3;
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX struct bench_status peer_status;

static int control(uint8_t request, uint16_t value, uint16_t index, void *data, uint16_t len)
{
    struct usbh_hubport *hp = peer;
    if (!hp || !hp->connected) return -USB_ERR_NODEV;
    hp->setup->bmRequestType = len ? 0xc0 : 0x40;
    hp->setup->bRequest = request;
    hp->setup->wValue = value;
    hp->setup->wIndex = index;
    hp->setup->wLength = len;
    return usbh_control_transfer(hp, hp->setup, data);
}

static int run_one(unsigned direction, unsigned round)
{
    struct usbh_urb *urb = &bulk_urbs[direction];
    unsigned shift = 0;
    while ((1u << shift) < run_block) ++shift;
    memset(&state, 0, sizeof(state));
    state.magic = BENCH_MAGIC; state.direction = direction;
    state.total = run_mib * 1024u * 1024u; state.block = run_block;
    prepare_data(run_block);
    int ret = control(BENCH_START, (shift << 8) | (run_verify << 1) | direction,
                      run_mib, NULL, 0);
    if (ret < 0) { printf("[usbbench] START failed=%d\n", ret); return ret; }
    TickType_t start = xTaskGetTickCount();
    while (state.bytes < state.total && peer && peer->connected) {
        if (!direction) set_header(state.bytes, run_block);
        usbh_bulk_urb_fill(urb, peer, direction ? ep_in : ep_out,
                          (uint8_t *)data_buffer, run_block, 3000, NULL, NULL);
        ret = usbh_submit_urb(urb);
        if (ret < 0 || urb->actual_length != run_block) {
            ++state.io_errors;
            printf("[usbbench] transfer failed=%d actual=%lu at=%lu\n", ret,
                   (unsigned long)urb->actual_length, (unsigned long)state.bytes);
            break;
        }
        if (direction) state.bad_words += check_data(state.bytes, run_block, run_verify);
        state.bytes += run_block; ++state.packets;
    }
    uint32_t ms = (xTaskGetTickCount() - start) * portTICK_PERIOD_MS;
    memset(&peer_status, 0, sizeof(peer_status));
    int status_ret = control(BENCH_STATUS, 0, 0, &peer_status, sizeof(peer_status));
    /* Both pinned EHCI and DWC2 drivers include the 8-byte SETUP transaction
     * in a control URB's actual_length; bulk URBs contain payload only. */
    unsigned ok = status_ret == sizeof(peer_status) + sizeof(struct usb_setup_packet)
        && peer_status.magic == BENCH_MAGIC
        && peer_status.direction == state.direction && peer_status.total == state.total
        && peer_status.block == state.block
        && state.bytes == state.total && peer_status.bytes == state.bytes
        && peer_status.packets == state.packets && !state.bad_words && !state.io_errors
        && !peer_status.bad_words && !peer_status.io_errors;
    uint32_t kbps = ms ? (uint64_t)state.bytes * 8 / ms : 0;
    printf("USB_BENCH_RESULT {\"host\":\"%s\",\"round\":%u,\"direction\":\"%s\","
           "\"speed\":%u,\"block\":%u,\"verify\":%u,\"bytes\":%lu,\"peer_bytes\":%lu,"
           "\"ms\":%lu,\"kbps\":%lu,\"bad_words\":%lu,\"peer_bad_words\":%lu,"
           "\"io_errors\":%lu,\"status_rc\":%d,\"ok\":%u}\n",
           USB_BENCH_BOARD, round, direction ? "device_to_host" : "host_to_device",
           peer ? peer->speed : 0, run_block, run_verify,
           (unsigned long)state.bytes, (unsigned long)peer_status.bytes, (unsigned long)ms,
           (unsigned long)kbps, (unsigned long)state.bad_words,
           (unsigned long)peer_status.bad_words, (unsigned long)state.io_errors, status_ret, ok);
    return ok ? 0 : -1;
}

static void benchmark_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int ret = 0;
        for (unsigned r = 1; r <= run_rounds && !ret; ++r) {
            for (unsigned d = 0; d < 2 && !ret; ++d) {
                ret = run_one(d, r);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        printf("[usbbench] suite %s\n", ret ? "FAIL" : "PASS");
        busy = 0;
    }
}

static int connected(struct usbh_hubport *hp, uint8_t intf)
{
    if (hp->device_desc.idVendor != 0xffff || hp->device_desc.idProduct != 0xb017 || peer)
        return -USB_ERR_NOTSUPP;
    struct usbh_interface_altsetting *alt = &hp->config.intf[intf].altsetting[0];
    ep_in = ep_out = NULL;
    for (unsigned i = 0; i < alt->intf_desc.bNumEndpoints; ++i) {
        struct usb_endpoint_descriptor *ep = &alt->ep[i].ep_desc;
        if (USB_GET_ENDPOINT_TYPE(ep->bmAttributes) != USB_ENDPOINT_TYPE_BULK) continue;
        if (ep->bEndpointAddress & 0x80) ep_in = ep; else ep_out = ep;
    }
    if (!ep_in || !ep_out) return -USB_ERR_INVAL;
    memset(bulk_urbs, 0, sizeof(bulk_urbs));
    hp->config.intf[intf].priv = &state;
    peer = hp;
    printf("[usbbench] peer ready speed=%u in=%02x/%u out=%02x/%u\n", hp->speed,
           ep_in->bEndpointAddress, ep_in->wMaxPacketSize,
           ep_out->bEndpointAddress, ep_out->wMaxPacketSize);
    return 0;
}
static int disconnected(struct usbh_hubport *hp, uint8_t intf)
{
    (void)intf;
    if (peer == hp) {
        peer = NULL;
        usbh_kill_urb(&bulk_urbs[0]);
        usbh_kill_urb(&bulk_urbs[1]);
    }
    printf("[usbbench] peer disconnected\n");
    return 0;
}
static const struct usbh_class_driver driver = {
    .driver_name = "usbbench", .connect = connected, .disconnect = disconnected
};
CLASS_INFO_DEFINE const struct usbh_class_info bench_class = {
    .match_flags = USB_CLASS_MATCH_INTF_CLASS | USB_CLASS_MATCH_INTF_SUBCLASS | USB_CLASS_MATCH_INTF_PROTOCOL,
    .bInterfaceClass = 0xff, .bInterfaceSubClass = 0x42, .bInterfaceProtocol = 1,
    .id_table = NULL, .class_driver = &driver
};

int usb_bench_command(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "run")) {
        if (busy || !peer) { printf("[usbbench] busy=%u connected=%u\n", busy, peer != NULL); return -1; }
        unsigned mib = argc > 2 ? strtoul(argv[2], NULL, 0) : 64;
        unsigned block = argc > 3 ? strtoul(argv[3], NULL, 0) : BENCH_MAX_BLOCK;
        unsigned verify = argc > 4 ? strtoul(argv[4], NULL, 0) : 1;
        unsigned rounds = argc > 5 ? strtoul(argv[5], NULL, 0) : 3;
        if (!mib || mib > 256 || block < 512 || block > BENCH_MAX_BLOCK || (block & (block - 1))
            || verify > 1 || !rounds || rounds > 20) return -1;
        run_mib = mib; run_block = block; run_verify = verify; run_rounds = rounds;
        busy = 1; xTaskNotifyGive(worker);
    } else {
        printf("[usbbench] %s HOST connected=%u busy=%u; usbbench run [MiB=64] [block=16384] [verify=1] [rounds=3]\n",
               USB_BENCH_BOARD, peer != NULL, busy);
    }
    return 0;
}
void usb_bench_init(uintptr_t base)
{
    configASSERT(xTaskCreate(benchmark_task, "usbbench", 2048, NULL, 5, &worker) == pdPASS);
    int ret = usbh_initialize(0, base, NULL);
    printf("[usbbench] %s HOST init=%d\n", USB_BENCH_BOARD, ret);
}

#else
static const uint8_t device_desc[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0, 0, 0, 0xffff, 0xb017, 0x0100, 1)
};
#define BENCH_CONFIG(type, mps) \
    9, type, 32, 0, 1, 1, 0, 0xc0, 0, \
    9, USB_DESCRIPTOR_TYPE_INTERFACE, 0, 0, 2, 0xff, 0x42, 1, 0, \
    7, USB_DESCRIPTOR_TYPE_ENDPOINT, BENCH_OUT, 2, (mps) & 255, (mps) >> 8, 0, \
    7, USB_DESCRIPTOR_TYPE_ENDPOINT, BENCH_IN, 2, (mps) & 255, (mps) >> 8, 0
static const uint8_t config_hs[] = { BENCH_CONFIG(USB_DESCRIPTOR_TYPE_CONFIGURATION, 512) };
static const uint8_t config_fs[] = { BENCH_CONFIG(USB_DESCRIPTOR_TYPE_CONFIGURATION, 64) };
static const uint8_t other_hs[] = { BENCH_CONFIG(USB_DESCRIPTOR_TYPE_OTHER_SPEED, 64) };
static const uint8_t other_fs[] = { BENCH_CONFIG(USB_DESCRIPTOR_TYPE_OTHER_SPEED, 512) };
static const uint8_t qualifier[] = { USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(USB_2_0, 0, 0, 0, 1) };
static const uint8_t *get_device(uint8_t speed) { (void)speed; return device_desc; }
static const uint8_t *get_config(uint8_t speed) { return speed == USB_SPEED_HIGH ? config_hs : config_fs; }
static const uint8_t *get_other(uint8_t speed) { return speed == USB_SPEED_HIGH ? other_hs : other_fs; }
static const uint8_t *get_qualifier(uint8_t speed) { (void)speed; return qualifier; }
static const char *get_string(uint8_t speed, uint8_t index)
{
    (void)speed;
    static const char *const strings[] = { "\x09\x04", "CherryUSB", USB_BENCH_BOARD " Bulk benchmark", USB_BENCH_BOARD "-USB-BENCH" };
    return index < 4 ? strings[index] : NULL;
}
static const struct usb_descriptor descriptors = {
    .device_descriptor_callback = get_device, .config_descriptor_callback = get_config,
    .device_quality_descriptor_callback = get_qualifier, .other_speed_descriptor_callback = get_other,
    .string_descriptor_callback = get_string
};
static volatile unsigned configured;
static int arm_transfer(void)
{
    if (state.direction) {
        set_header(state.bytes, state.block);
        return usbd_ep_start_write(0, BENCH_IN, (uint8_t *)data_buffer, state.block);
    }
    return usbd_ep_start_read(0, BENCH_OUT, (uint8_t *)data_buffer, state.block);
}
static void completed(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid; (void)ep;
    if (state.bytes >= state.total || nbytes != state.block) { ++state.io_errors; return; }
    if (!state.direction) state.bad_words += check_data(state.bytes, state.block, verify_data);
    state.bytes += nbytes; ++state.packets;
    if (state.bytes < state.total && arm_transfer() < 0) ++state.io_errors;
}
static int vendor_request(uint8_t busid, struct usb_setup_packet *setup, uint8_t **data, uint32_t *len)
{
    (void)busid;
    if (setup->bRequest == BENCH_START && setup->bmRequestType == 0x40 && !setup->wLength) {
        unsigned shift = setup->wValue >> 8;
        if (!configured || shift < 9 || shift > 14 || !setup->wIndex || setup->wIndex > 256
            || (setup->wValue & 0xfc) || state.bytes < state.total) return -1;
        memset(&state, 0, sizeof(state));
        state.magic = BENCH_MAGIC; state.direction = setup->wValue & 1;
        state.total = setup->wIndex * 1024u * 1024u; state.block = 1u << shift;
        verify_data = (setup->wValue >> 1) & 1;
        prepare_data(state.block);
        int ret = arm_transfer();
        if (ret < 0) ++state.io_errors;
        return ret;
    }
    if (setup->bRequest == BENCH_STATUS && setup->bmRequestType == 0xc0
        && setup->wLength == sizeof(state)) {
        memcpy(*data, &state, sizeof(state)); *len = sizeof(state); return 0;
    }
    return -1;
}
static void event_handler(uint8_t busid, uint8_t event)
{
    if (event == USBD_EVENT_RESET || event == USBD_EVENT_DISCONNECTED) {
        configured = 0; memset(&state, 0, sizeof(state));
    }
    if (event == USBD_EVENT_CONFIGURED) {
        configured = 1;
        printf("[usbbench] %s DEVICE configured speed=%u\n", USB_BENCH_BOARD, usbd_get_port_speed(busid));
    }
}
static struct usbd_interface interface = { .vendor_handler = vendor_request };
static struct usbd_endpoint in_endpoint = { .ep_addr = BENCH_IN, .ep_cb = completed };
static struct usbd_endpoint out_endpoint = { .ep_addr = BENCH_OUT, .ep_cb = completed };
int usb_bench_command(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("[usbbench] %s DEVICE configured=%u direction=%lu bytes=%lu/%lu block=%lu packets=%lu bad_words=%lu io_errors=%lu\n",
           USB_BENCH_BOARD, configured, (unsigned long)state.direction, (unsigned long)state.bytes,
           (unsigned long)state.total, (unsigned long)state.block, (unsigned long)state.packets,
           (unsigned long)state.bad_words, (unsigned long)state.io_errors);
    return 0;
}
void usb_bench_init(uintptr_t base)
{
    usbd_desc_register(0, &descriptors);
    usbd_add_interface(0, &interface);
    usbd_add_endpoint(0, &in_endpoint); usbd_add_endpoint(0, &out_endpoint);
    int ret = usbd_initialize(0, base, event_handler);
    printf("[usbbench] %s DEVICE init=%d\n", USB_BENCH_BOARD, ret);
}
#endif
