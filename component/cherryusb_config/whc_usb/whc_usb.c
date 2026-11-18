/* Native WHC USB host. Independent IN/OUT workers; no bridge polling frames.
 * Use one OUT endpoint to preserve Ethernet/control ordering. Endpoint 0x84
 * carries device events and Ethernet RX. DMA buffers stay in uncached OCRAM. */
#include "whc_usb.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "usbh_core.h"
#include <stdio.h>
#include <string.h>
#ifdef CONFIG_NAN_USB_DIAGNOSTICS
#include "bflb_mtimer.h"
#include "bl618dg_clock.h"
extern volatile uint32_t bl618dg_ehci_irq_completions, bl618dg_ehci_poll_completions, bl618dg_ehci_poll_checks;
struct timing { uint64_t sum; unsigned count, maximum, bin[6]; };
static struct timing tx_time, rx_dispatch_time;
static void record_time(struct timing *s, uint64_t started)
{
    uint64_t us = bflb_mtimer_get_time_us() - started;
    s->sum += us; ++s->count;
    if (us > s->maximum) s->maximum = us > UINT32_MAX ? UINT32_MAX : us;
    unsigned bin = us < 100 ? 0 : us < 250 ? 1 : us < 500 ? 2 : us < 1000 ? 3 : us < 2000 ? 4 : 5;
    ++s->bin[bin];
}
static void print_time(const char *name, const struct timing *live)
{
    taskENTER_CRITICAL(); struct timing s = *live; taskEXIT_CRITICAL();
    printf("[WHC-PERF] %s count=%u avg_us=%llu max_us=%u bins_lt100/250/500/1000/2000/over=%u/%u/%u/%u/%u/%u\n",
        name, s.count, (unsigned long long)(s.count ? s.sum / s.count : 0), s.maximum,
        s.bin[0],s.bin[1],s.bin[2],s.bin[3],s.bin[4],s.bin[5]);
}
#endif

struct message { size_t size; uint8_t data[]; };
static QueueHandle_t control_queue, data_queue;
static TaskHandle_t tx_worker;
static SemaphoreHandle_t api_lock, api_done;
static struct usbh_hubport *volatile peer;
static struct usb_endpoint_descriptor *ep_in, *ep_out;
static struct usbh_urb in_urb, out_urb;
static volatile unsigned connected, waiting_api;
static int api_result;
static whc_usb_receive_fn receive_frame;
static unsigned tx_packets, rx_packets, drops, invalid, io_errors, rx_timeouts;
static uint64_t tx_bytes, rx_bytes;
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t tx_buffer[WHC_USB_MTU];
/* Round to HS packet size; never allow a malicious oversized device message. */
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t rx_buffer[2048];

int whc_usb_connected(void) { return connected != 0; }
static void dispose(struct message *m)
{
    volatile uint8_t *p = m->data;
    for (size_t i = 0; i < m->size; ++i) p[i] = 0;
    vPortFree(m);
}
static void fail_connection(int rc)
{
    taskENTER_CRITICAL();
    connected = 0;
    if (waiting_api) { api_result = -1; waiting_api = 0; xSemaphoreGive(api_done); }
    taskEXIT_CRITICAL();
    ++io_errors;
    printf("[WHC-USB] transport stopped rc=%d; reconnect/reset required\n", rc);
}
int whc_usb_send(uint32_t event, uint32_t code, const void *data, size_t length)
{
    if (!connected || length > WHC_USB_OUT_MTU - WHC_HEADER_SIZE || (length && !data)) return -1;
    struct message *m = pvPortMalloc(sizeof(*m) + WHC_HEADER_SIZE + length);
    if (!m) { ++drops; return -1; }
    m->size = whc_frame_build(m->data, event, code, data, length);
    QueueHandle_t q = event == WHC_TX_PACKET ? data_queue : control_queue;
    if (!m->size || xQueueSend(q, &m, 0) != pdPASS) { ++drops; dispose(m); return -1; }
    xTaskNotifyGive(tx_worker);
    return 0;
}
int whc_usb_call(uint32_t api, const void *data, size_t length)
{
    if (!api || xSemaphoreTake(api_lock, pdMS_TO_TICKS(6000)) != pdPASS) return -1;
    while (xSemaphoreTake(api_done, 0) == pdPASS) {}
    waiting_api = api; api_result = -1;
    int rc = whc_usb_send(WHC_API_CALL, api, data, length);
    if (!rc) {
        if (xSemaphoreTake(api_done, pdMS_TO_TICKS(5000)) == pdPASS) rc = api_result;
        else {
            /* WHC has no transaction sequence. Never accept a late reply as a
             * later call to the same API after a timeout. */
            printf("[WHC-USB] API timeout id=%04x\n", (unsigned)api);
            fail_connection(-USB_ERR_TIMEOUT); rc = -1;
        }
    }
    taskENTER_CRITICAL(); waiting_api = 0; taskEXIT_CRITICAL();
    xSemaphoreGive(api_lock);
    return rc;
}
static void tx_task(void *arg)
{
    (void)arg;
    for (;;) {
        struct message *m;
        if (xQueueReceive(control_queue, &m, 0) != pdPASS &&
            xQueueReceive(data_queue, &m, 0) != pdPASS) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY); continue;
        }
        if (!connected || !peer) { dispose(m); continue; }
        memcpy(tx_buffer, m->data, m->size);
        size_t size = m->size; dispose(m);
#ifdef CONFIG_NAN_USB_DIAGNOSTICS
        uint64_t started = bflb_mtimer_get_time_us();
#endif
        usbh_bulk_urb_fill(&out_urb, peer, ep_out, tx_buffer, size, 3000, NULL, NULL);
        int rc = usbh_submit_urb(&out_urb);
        if (!rc && out_urb.actual_length != size) rc = -USB_ERR_IO;
        if (!rc && size % 512 == 0) {
            /* Native device receives to a larger buffer: terminate exact MPS. */
            usbh_bulk_urb_fill(&out_urb, peer, ep_out, tx_buffer, 0, 3000, NULL, NULL);
            rc = usbh_submit_urb(&out_urb);
        }
        if (rc) fail_connection(rc);
        else { ++tx_packets; tx_bytes += size; }
#ifdef CONFIG_NAN_USB_DIAGNOSTICS
        record_time(&tx_time, started);
#endif
    }
}
static void rx_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (!connected || !peer) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        usbh_bulk_urb_fill(&in_urb, peer, ep_in, rx_buffer, sizeof(rx_buffer), 2000, NULL, NULL);
        int rc = usbh_submit_urb(&in_urb);
        if (!connected) continue;
        if (rc == -USB_ERR_TIMEOUT) { ++rx_timeouts; continue; }
        if (rc) { fail_connection(rc); continue; }
        if (!in_urb.actual_length) continue; /* device ZLP */
        struct whc_frame f;
        if (whc_frame_parse(rx_buffer, in_urb.actual_length, &f)) {
            ++invalid;
            printf("[WHC-USB] invalid frame len=%u event=%08x\n",
                (unsigned)in_urb.actual_length, (unsigned)whc_get32(rx_buffer));
            continue;
        }
        ++rx_packets; rx_bytes += in_urb.actual_length;
        if (f.event == WHC_API_REPLY) {
            taskENTER_CRITICAL();
            if (waiting_api && waiting_api == f.code) {
                api_result = f.length == 4 ? (int32_t)whc_get32(f.data) : -1;
                waiting_api = 0; xSemaphoreGive(api_done);
            }
            taskEXIT_CRITICAL();
        } else if (receive_frame) {
#ifdef CONFIG_NAN_USB_DIAGNOSTICS
            uint64_t started = bflb_mtimer_get_time_us();
#endif
            receive_frame(&f);
#ifdef CONFIG_NAN_USB_DIAGNOSTICS
            record_time(&rx_dispatch_time, started);
#endif
        }
    }
}
static int attach(struct usbh_hubport *hp, uint8_t intf)
{
    if (peer || hp->device_desc.idVendor != 0x0bda ||
        (hp->device_desc.idProduct != 0xf851 && hp->device_desc.idProduct != 0x8006)) return -USB_ERR_NOTSUPP;
    struct usbh_interface_altsetting *alt = &hp->config.intf[intf].altsetting[0];
    ep_in = ep_out = NULL;
    for (unsigned i = 0; i < alt->intf_desc.bNumEndpoints; ++i) {
        struct usb_endpoint_descriptor *ep = &alt->ep[i].ep_desc;
        if (USB_GET_ENDPOINT_TYPE(ep->bmAttributes) != USB_ENDPOINT_TYPE_BULK) continue;
        if (ep->bEndpointAddress == 0x84) ep_in = ep;
        if (ep->bEndpointAddress == 0x05) ep_out = ep;
    }
    if (!ep_in || !ep_out || hp->speed != USB_SPEED_HIGH) return -USB_ERR_INVAL;
    memset(&in_urb, 0, sizeof(in_urb)); memset(&out_urb, 0, sizeof(out_urb));
    peer = hp; connected = 1;
    printf("[WHC-USB] native Green2 attached High-Speed VID=%04x PID=%04x\n",
        hp->device_desc.idVendor, hp->device_desc.idProduct);
    return 0;
}
static int detach(struct usbh_hubport *hp, uint8_t intf)
{
    (void)intf;
    if (peer == hp) {
        fail_connection(-USB_ERR_NODEV);
        usbh_kill_urb(&in_urb); usbh_kill_urb(&out_urb);
        peer = NULL;
    }
    return 0;
}
static const struct usbh_class_driver driver = { .driver_name="whc_usb", .connect=attach, .disconnect=detach };
CLASS_INFO_DEFINE const struct usbh_class_info whc_usb_class = {
    .match_flags = USB_CLASS_MATCH_INTF_CLASS | USB_CLASS_MATCH_INTF_SUBCLASS | USB_CLASS_MATCH_INTF_PROTOCOL,
    .bInterfaceClass=0xff, .bInterfaceSubClass=0, .bInterfaceProtocol=1,
    .id_table=NULL, .class_driver=&driver
};
void whc_usb_init(uintptr_t base, whc_usb_receive_fn receive)
{
    receive_frame = receive;
    control_queue = xQueueCreate(24, sizeof(struct message *));
    data_queue = xQueueCreate(128, sizeof(struct message *));
    api_lock = xSemaphoreCreateMutex(); api_done = xSemaphoreCreateBinary();
    configASSERT(control_queue && data_queue && api_lock && api_done);
    configASSERT(xTaskCreate(tx_task, "whc_tx", 2048, NULL, 5, &tx_worker) == pdPASS);
    configASSERT(xTaskCreate(rx_task, "whc_rx", 2048, NULL, 5, NULL) == pdPASS);
    configASSERT(usbh_initialize(0, base, NULL) == 0);
}
void whc_usb_status(void)
{
    printf("[WHC-USB] connected=%u tx=%u/%llu rx=%u/%llu drop=%u bad=%u io=%u idle=%u q=%u/%u\n",
        connected, tx_packets, (unsigned long long)tx_bytes, rx_packets, (unsigned long long)rx_bytes,
        drops, invalid, io_errors, rx_timeouts, (unsigned)uxQueueMessagesWaiting(control_queue),
        (unsigned)uxQueueMessagesWaiting(data_queue));
#ifdef CONFIG_NAN_USB_DIAGNOSTICS
    printf("[WHC-PERF] cpu_hz=%u tick_hz=%u IRQ completions=%u poll completions=%u poll checks=%u\n",
        (unsigned)Clock_System_Clock_Get(BL_SYSTEM_CLOCK_MCU_CLK), (unsigned)configTICK_RATE_HZ,
        (unsigned)bl618dg_ehci_irq_completions, (unsigned)bl618dg_ehci_poll_completions,
        (unsigned)bl618dg_ehci_poll_checks);
    print_time("OUT", &tx_time); print_time("RX dispatch", &rx_dispatch_time);
#endif
}
