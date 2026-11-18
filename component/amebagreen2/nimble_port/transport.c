#include <stdbool.h>
#include <string.h>
#include "os/os.h"
#include "nimble/transport.h"
#include "nimble/transport_impl.h"
#include "nimble/nimble_port.h"
#include "rtk_bt_gap.h"
#include "FreeRTOS.h"
#include "task.h"
#include "hci_common.h"
#include "hci_controller.h"
#include "hci_transport.h"

static bool host_ready;
struct vendor_request {
    struct ble_npl_event event;
    uint16_t opcode;
    uint8_t len;
    uint8_t data[];
};
extern int ble_hs_hci_cmd_tx(uint16_t, const void *, uint8_t, void *, uint8_t);
static void vendor_event(struct ble_npl_event *event)
{
    struct vendor_request *req = ble_npl_event_get_arg(event);
    (void)ble_hs_hci_cmd_tx(req->opcode, req->data, req->len, NULL, 0);
    vPortFree(req);
}
/* RTK coex runs on the HCI RX path. Queue its vendor command to the NimBLE
 * host so waiting for the command-complete cannot block RX itself. */
uint16_t rtk_bt_gap_vendor_cmd_req(rtk_bt_gap_vendor_cmd_param_t *param)
{
    if (!host_ready || !param) return 1;
    struct vendor_request *req = pvPortMalloc(sizeof(*req) + param->len);
    if (!req) return 1;
    req->opcode = param->op; req->len = param->len;
    memcpy(req->data, param->cmd_param, param->len);
    ble_npl_event_init(&req->event, vendor_event, req);
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &req->event);
    return 0;
}

static void receive(struct hci_rx_packet_t *packet)
{
    if (packet->type == HCI_EVT) {
        void *evt;
        /* Only advertising reports may be dropped; preserve command completions
         * with task-context backpressure until the host releases an event. */
        while (!(evt = ble_transport_alloc_evt(packet->discardable))) {
            if (packet->discardable) goto done;
            vTaskDelay(1);
        }
        memcpy(evt, packet->buf, packet->len);
        ble_transport_to_hs_evt(evt);
    } else if (packet->type == HCI_ACL) {
        struct os_mbuf *om;
        while (!(om = ble_transport_alloc_acl_from_ll())) vTaskDelay(1);
        if (os_mbuf_append(om, packet->buf, packet->len)) os_mbuf_free_chain(om);
        else ble_transport_to_hs_acl(om);
    }
done:
    hci_rx_pkt_free(packet);
}
static struct hci_transport_cb callbacks = { .recv = receive };

void nimble_platform_controller_bringup(void)
{
#ifdef AT_GREEN2_BLE_SLEEP
    extern void hci_platform_set_tx_power_gain_index(uint32_t);
    /* Controller gain configuration uses 0.5 dBm units: 0x10 = 8 dBm. */
    hci_platform_set_tx_power_gain_index(0x10);
#endif
    configASSERT(hci_controller_open());
}
void ble_transport_ll_init(void) {}
void nimble_platform_transport_start_rx(void)
{
    host_ready = true;
    hci_transport_register(&callbacks);
}
void nimble_platform_transport_stop_rx(void)
{
    host_ready = false;
    hci_transport_register(NULL);
    hci_controller_close();
    hci_controller_free();
}
int ble_transport_to_ll_cmd_impl(void *buf)
{
    uint8_t *p = buf;
    uint16_t n = 3 + p[2];
    uint16_t sent = hci_transport_send(HCI_CMD, p, n, false);
    ble_transport_free(buf);
    return sent == n ? 0 : -1;
}
int ble_transport_to_ll_acl_impl(struct os_mbuf *om)
{
    uint16_t n = OS_MBUF_PKTLEN(om);
    uint8_t *data = pvPortMalloc(n);
    int rc = -1;
    if (data) {
        if (!os_mbuf_copydata(om, 0, n, data))
            rc = hci_transport_send(HCI_ACL, data, n, false) == n ? 0 : -1;
        vPortFree(data);
    }
    os_mbuf_free_chain(om);
    return rc;
}
