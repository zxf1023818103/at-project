/* Fixed-size, validated USB exchanges aggregate control records and Ethernet
 * packets. The Green2 worker owns IPC calls; USB interrupt handlers only wake it.
 * No pointer or MCU address is serialized. No payload/key logging. */
#include "nan_usb.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "usbd_core.h"
#include "usbh_core.h"
#include <stdio.h>
#include <string.h>

#define EP_IN 0x84u
#define EP_OUT 0x05u
struct message { struct nan_usb_record record; uint8_t data[]; };
static QueueHandle_t control_queue, data_queue;
static struct message *pending;
static TaskHandle_t worker;
static nan_usb_receive_fn receive_record;
static volatile unsigned connected;
static unsigned tx_records, rx_records, dropped, bad_frames, io_errors;
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t tx_frame[NAN_USB_FRAME_SIZE];
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t rx_frame[NAN_USB_FRAME_SIZE];

static void dispose(struct message *m)
{
    volatile uint8_t *p = m->data;
    for (size_t i = 0; i < m->record.length; ++i) p[i] = 0;
    vPortFree(m);
}
int nan_usb_connected(void) { return connected != 0; }
int nan_usb_send(uint16_t type, uint16_t code, uint32_t id, int32_t status,
                 const void *data, size_t length)
{
    if (!connected || length > NAN_USB_MAX_PAYLOAD || (length && !data)) return -1;
    struct message *m = pvPortMalloc(sizeof(*m) + length);
    if (!m) { ++dropped; return -1; }
    m->record = (struct nan_usb_record){type, code, id, length, status};
    if (length) memcpy(m->data, data, length);
    QueueHandle_t q = type == NAN_USB_ETHERNET ? data_queue : control_queue;
    if (xQueueSend(q, &m, 0) != pdPASS) { ++dropped; dispose(m); return -1; }
    return 0;
}
static void clear_queues(void)
{
    if (pending) { dispose(pending); pending = NULL; }
    struct message *m;
    while (xQueueReceive(control_queue, &m, 0) == pdPASS) dispose(m);
    while (xQueueReceive(data_queue, &m, 0) == pdPASS) dispose(m);
}
static unsigned build_frame(uint32_t sequence)
{
    unsigned count = 0;
    nan_usb_frame_init(tx_frame, sequence);
    for (;;) {
        if (!pending && xQueueReceive(control_queue, &pending, 0) != pdPASS &&
            xQueueReceive(data_queue, &pending, 0) != pdPASS) break;
        if (nan_usb_frame_append(tx_frame, &pending->record, pending->data)) break;
        dispose(pending); pending = NULL; ++count;
    }
    tx_records += count;
    return count;
}

#if NAN_USB_HOST
static struct usbh_hubport *volatile peer;
static struct usb_endpoint_descriptor *ep_in, *ep_out;
static struct usbh_urb in_urb, out_urb;
static SemaphoreHandle_t rpc_lock, rpc_done;
static uint32_t next_id;
static volatile uint32_t waiting_id;
static struct {
    int status;
    size_t length;
    uint8_t data[NAN_USB_MAX_PAYLOAD];
} reply;
static void dispatch(const struct nan_usb_record *r, const void *data)
{
    if (r->type == NAN_USB_REPLY) {
        if (r->id != waiting_id || !waiting_id) return;
        reply.status = r->status; reply.length = r->length;
        memcpy(reply.data, data, r->length);
        waiting_id = 0;
        xSemaphoreGive(rpc_done);
    } else if (receive_record) receive_record(r, data);
}
int nan_usb_rpc(uint16_t command, const void *input, size_t input_size,
                void *output, size_t *output_size)
{
    if (input_size > NAN_USB_MAX_PAYLOAD || xSemaphoreTake(rpc_lock, pdMS_TO_TICKS(6000)) != pdPASS) return -1;
    while (xSemaphoreTake(rpc_done, 0) == pdPASS) {}
    if (++next_id == 0) ++next_id;
    waiting_id = next_id;
    int rc = nan_usb_send(NAN_USB_RPC, command, waiting_id, 0, input, input_size);
    if (!rc && xSemaphoreTake(rpc_done, pdMS_TO_TICKS(5000)) == pdPASS) {
        rc = reply.status;
        if (!rc && output_size) {
            if (reply.length > *output_size || (reply.length && !output)) rc = -1;
            else { if (reply.length) memcpy(output, reply.data, reply.length); *output_size = reply.length; }
        }
    } else rc = -1;
    taskENTER_CRITICAL(); waiting_id = 0; taskEXIT_CRITICAL();
    volatile uint8_t *p = reply.data;
    for (size_t i = 0; i < sizeof(reply.data); ++i) p[i] = 0;
    xSemaphoreGive(rpc_lock);
    return rc;
}
#else
static volatile unsigned epoch;
static void dispatch(const struct nan_usb_record *r, const void *data)
{ if (receive_record) receive_record(r, data); }
#endif

static int parse_frame(size_t length)
{
    if (!nan_usb_frame_valid(rx_frame, length)) { ++bad_frames; return -1; }
    const struct nan_usb_frame *f = (const void *)rx_frame;
    for (size_t pos = sizeof(*f); pos < f->used;) {
        const struct nan_usb_record *r = (const void *)(rx_frame + pos);
        dispatch(r, rx_frame + pos + sizeof(*r));
        ++rx_records;
        pos += sizeof(*r) + ((r->length + 3u) & ~3u);
    }
    return 0;
}
void nan_usb_status(void)
{
    printf("[NAN-USB] connected=%u tx=%u rx=%u drop=%u bad=%u io=%u queued=%u/%u\n",
        connected, tx_records, rx_records, dropped, bad_frames, io_errors,
        (unsigned)uxQueueMessagesWaiting(control_queue), (unsigned)uxQueueMessagesWaiting(data_queue));
}

#if NAN_USB_HOST
static int attach(struct usbh_hubport *hp, uint8_t intf)
{
    if (peer || hp->device_desc.idVendor != 0xffff || hp->device_desc.idProduct != 0xb018) return -USB_ERR_NOTSUPP;
    struct usbh_interface_altsetting *alt = &hp->config.intf[intf].altsetting[0];
    ep_in = ep_out = NULL;
    for (unsigned i = 0; i < alt->intf_desc.bNumEndpoints; ++i) {
        struct usb_endpoint_descriptor *ep = &alt->ep[i].ep_desc;
        if (USB_GET_ENDPOINT_TYPE(ep->bmAttributes) != USB_ENDPOINT_TYPE_BULK) continue;
        if (ep->bEndpointAddress == EP_IN) ep_in = ep;
        if (ep->bEndpointAddress == EP_OUT) ep_out = ep;
    }
    if (!ep_in || !ep_out || hp->speed != USB_SPEED_HIGH) return -USB_ERR_INVAL;
    memset(&in_urb, 0, sizeof(in_urb)); memset(&out_urb, 0, sizeof(out_urb));
    peer = hp; connected = 1;
    printf("[NAN-USB] Green2 radio attached High-Speed\n");
    return 0;
}
static int detach(struct usbh_hubport *hp, uint8_t intf)
{
    (void)intf;
    if (peer == hp) {
        connected = 0; peer = NULL;
        usbh_kill_urb(&in_urb); usbh_kill_urb(&out_urb);
        if (waiting_id) { reply.status = -1; reply.length = 0; waiting_id = 0; xSemaphoreGive(rpc_done); }
    }
    printf("[NAN-USB] radio detached\n");
    return 0;
}
static const struct usbh_class_driver driver = {.driver_name = "nan_usb", .connect = attach, .disconnect = detach};
CLASS_INFO_DEFINE const struct usbh_class_info nan_usb_class = {
    .match_flags = USB_CLASS_MATCH_INTF_CLASS | USB_CLASS_MATCH_INTF_SUBCLASS | USB_CLASS_MATCH_INTF_PROTOCOL,
    .bInterfaceClass = 0xff, .bInterfaceSubClass = 0x42, .bInterfaceProtocol = 2,
    .id_table = NULL, .class_driver = &driver
};
static void transport_task(void *arg)
{
    (void)arg;
    uint32_t sequence = 0;
    for (;;) {
        if (!connected || !peer) { clear_queues(); vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        unsigned sent = build_frame(++sequence);
        usbh_bulk_urb_fill(&out_urb, peer, ep_out, tx_frame, sizeof(tx_frame), 3000, NULL, NULL);
        int rc = usbh_submit_urb(&out_urb);
        if (!rc && out_urb.actual_length == sizeof(tx_frame)) {
            usbh_bulk_urb_fill(&in_urb, peer, ep_in, rx_frame, sizeof(rx_frame), 3000, NULL, NULL);
            rc = usbh_submit_urb(&in_urb);
            if (!rc && (in_urb.actual_length != sizeof(rx_frame) ||
                        ((struct nan_usb_frame *)rx_frame)->sequence != sequence || parse_frame(in_urb.actual_length))) rc = -1;
        } else rc = -1;
        if (rc) {
            ++io_errors; connected = 0;
            printf("[NAN-USB] exchange failed=%d; reset/reconnect radio\n", rc);
        } else if (!sent && ((struct nan_usb_frame *)rx_frame)->used == sizeof(struct nan_usb_frame)) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}
#else
static const uint8_t device_desc[] = { USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0, 0, 0, 0xffff, 0xb018, 0x0100, 1) };
#define CONFIG_DESC(type, mps) \
    9, type, 32, 0, 1, 1, 0, 0xc0, 0, \
    9, USB_DESCRIPTOR_TYPE_INTERFACE, 0, 0, 2, 0xff, 0x42, 2, 0, \
    7, USB_DESCRIPTOR_TYPE_ENDPOINT, EP_OUT, 2, (mps)&255, (mps)>>8, 0, \
    7, USB_DESCRIPTOR_TYPE_ENDPOINT, EP_IN, 2, (mps)&255, (mps)>>8, 0
static const uint8_t config_hs[] = { CONFIG_DESC(USB_DESCRIPTOR_TYPE_CONFIGURATION, 512) };
static const uint8_t config_fs[] = { CONFIG_DESC(USB_DESCRIPTOR_TYPE_CONFIGURATION, 64) };
static const uint8_t other_hs[] = { CONFIG_DESC(USB_DESCRIPTOR_TYPE_OTHER_SPEED, 64) };
static const uint8_t other_fs[] = { CONFIG_DESC(USB_DESCRIPTOR_TYPE_OTHER_SPEED, 512) };
static const uint8_t qualifier[] = { USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(USB_2_0, 0, 0, 0, 1) };
static const uint8_t *get_device(uint8_t s) { (void)s; return device_desc; }
static const uint8_t *get_config(uint8_t s) { return s == USB_SPEED_HIGH ? config_hs : config_fs; }
static const uint8_t *get_other(uint8_t s) { return s == USB_SPEED_HIGH ? other_hs : other_fs; }
static const uint8_t *get_qualifier(uint8_t s) { (void)s; return qualifier; }
static const char *get_string(uint8_t s, uint8_t i)
{
    (void)s; static const char *const strings[] = {"\x09\x04", "CherryUSB", "Green2 NAN radio", "GREEN2-NAN-USB"};
    return i < 4 ? strings[i] : NULL;
}
static const struct usb_descriptor descriptors = {
    .device_descriptor_callback=get_device, .config_descriptor_callback=get_config,
    .device_quality_descriptor_callback=get_qualifier, .other_speed_descriptor_callback=get_other,
    .string_descriptor_callback=get_string
};
static void rx_done(uint8_t bus, uint8_t ep, uint32_t length)
{
    (void)bus; (void)ep;
    BaseType_t woken = pdFALSE;
    if (length != sizeof(rx_frame)) { ++bad_frames; return; }
    xTaskNotifyFromISR(worker, 1u, eSetBits, &woken); portYIELD_FROM_ISR(woken);
}
static void tx_done(uint8_t bus, uint8_t ep, uint32_t length)
{
    (void)bus; (void)ep;
    if (length != sizeof(tx_frame)) ++io_errors;
    if (connected && usbd_ep_start_read(0, EP_OUT, rx_frame, sizeof(rx_frame)) < 0) ++io_errors;
}
static void event(uint8_t bus, uint8_t event_id)
{
    (void)bus;
    if (event_id == USBD_EVENT_RESET || event_id == USBD_EVENT_DISCONNECTED) {
        connected = 0; ++epoch;
    } else if (event_id == USBD_EVENT_CONFIGURED) {
        connected = 1;
        if (usbd_ep_start_read(0, EP_OUT, rx_frame, sizeof(rx_frame)) < 0) ++io_errors;
        printf("[NAN-USB] radio bridge configured\n");
    }
}
static struct usbd_interface interface;
static struct usbd_endpoint in_endpoint = {.ep_addr=EP_IN, .ep_cb=tx_done};
static struct usbd_endpoint out_endpoint = {.ep_addr=EP_OUT, .ep_cb=rx_done};
static void transport_task(void *arg)
{
    (void)arg;
    unsigned previous_epoch = epoch;
    for (;;) {
        uint32_t notification;
        xTaskNotifyWait(0, UINT32_MAX, &notification, portMAX_DELAY);
        unsigned current_epoch = epoch;
        if (current_epoch != previous_epoch) { clear_queues(); previous_epoch = current_epoch; }
        if (!connected || !(notification & 1u)) continue;
        if (parse_frame(sizeof(rx_frame))) { connected = 0; continue; }
        build_frame(((struct nan_usb_frame *)rx_frame)->sequence);
        if (connected && current_epoch == epoch && usbd_ep_start_write(0, EP_IN, tx_frame, sizeof(tx_frame)) < 0) ++io_errors;
    }
}
#endif

void nan_usb_init(uintptr_t base, nan_usb_receive_fn receive)
{
    receive_record = receive;
    control_queue = xQueueCreate(24, sizeof(struct message *));
    data_queue = xQueueCreate(64, sizeof(struct message *));
    configASSERT(control_queue && data_queue);
#if NAN_USB_HOST
    rpc_lock = xSemaphoreCreateMutex(); rpc_done = xSemaphoreCreateBinary();
    configASSERT(rpc_lock && rpc_done);
#endif
    configASSERT(xTaskCreate(transport_task, "nan_usb", 2048, NULL, 5, &worker) == pdPASS);
#if NAN_USB_HOST
    configASSERT(usbh_initialize(0, base, NULL) == 0);
#else
    usbd_desc_register(0, &descriptors); usbd_add_interface(0, &interface);
    usbd_add_endpoint(0, &in_endpoint); usbd_add_endpoint(0, &out_endpoint);
    configASSERT(usbd_initialize(0, base, event) == 0);
#endif
}
