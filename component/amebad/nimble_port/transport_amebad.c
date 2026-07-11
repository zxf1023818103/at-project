/*
 * transport_amebad.c — NimBLE custom HCI transport (LL/controller side) over the
 * AmebaD HCI UART (component/amebad/hci). BLE_TRANSPORT_LL=custom.
 *
 * Mirrors upstream nimble/transport/uart_ll (the LL-side H4-over-UART transport):
 *   - RX (controller->host): a task drains our HCI UART ring (hci_uart_recv, filled
 *     by the hci_uart RX IRQ) and feeds the bytes to NimBLE's H4 state machine; its
 *     frame callback hands complete EVT/ACL packets to ble_transport_to_hs_*.
 *   - TX (host->controller): prepend the H4 type byte and push the packet to the
 *     controller UART byte-by-byte via hci_ctrl_putc() (blocking, host-task context).
 *
 * Reassembly + dispatch run in the RX TASK (never the IRQ) — the IRQ only fills the
 * ring. The controller must already be brought up (bt_dtm_bringup) before the host
 * starts; nimble_app.c orders that.
 */
#include "os/os.h"
#include "nimble/transport.h"
#include "nimble/transport_impl.h"
#include "nimble/transport/hci_h4.h"
#include "FreeRTOS.h"
#include "task.h"

/* component/amebad/hci public API */
extern unsigned short hci_uart_recv(unsigned char *p_buf, unsigned short size); /* ring read (non-blocking) */
extern void           hci_ctrl_putc(unsigned char b);                            /* blocking 1-byte -> UART1 */

static struct hci_h4_sm h4sm;

/* ---- RX: controller -> host ---- */
static int
nimble_amebad_frame_cb(uint8_t pkt_type, void *data)
{
    switch (pkt_type) {
    case HCI_H4_EVT:
        return ble_transport_to_hs_evt(data);
    case HCI_H4_ACL:
        return ble_transport_to_hs_acl(data);
    default:
        ble_transport_free(data);
        return -1;
    }
}

static void
nimble_amebad_rx_task(void *arg)
{
    uint8_t  buf[64];
    uint16_t n;
    (void)arg;

    for (;;) {
        n = hci_uart_recv(buf, sizeof(buf));
        if (n) {
            hci_h4_sm_rx(&h4sm, buf, n);
        } else {
            vTaskDelay(1);   /* nothing pending — yield */
        }
    }
}

/* Called by NimBLE sysinit (ble_transport_init -> ble_transport_ll_init). */
void
ble_transport_ll_init(void)
{
    hci_h4_sm_init(&h4sm, &hci_h4_allocs_from_ll, nimble_amebad_frame_cb);
}

static TaskHandle_t s_rx_task_h;

/* Started by nimble_app.c AFTER nimble_port_init (so the H4 SM is inited) and the
 * controller bring-up; pumps controller RX into the host. */
void
nimble_amebad_transport_start_rx(void)
{
    /* 4096 words: ble_transport_to_hs_acl() runs the whole controller->host RX chain
     * (H4 reassembly -> L2CAP -> ATT -> GATT -> the app's notify callback) INLINE in this
     * task, so a sustained notification flood (the bletp throughput RX side) needs deep
     * stack; 768 then 2048 both overflowed into the MSP region (SAU "Security Fault",
     * MSP==PSP, in hci_h4_sm_rx). */
    xTaskCreate(nimble_amebad_rx_task, "nimble_hci_rx", 4096, NULL,
                tskIDLE_PRIORITY + 4, &s_rx_task_h);
}

/* Delete the HCI RX task to reclaim its stack (~3 KB). Used by amebad_nimble_stop()
 * to free heap for the OTA HTTPS data-plane TLS. The task spends idle time in
 * vTaskDelay(1), so deleting it between polls is clean. */
void
nimble_amebad_transport_stop_rx(void)
{
    if (s_rx_task_h != NULL) {
        vTaskDelete(s_rx_task_h);
        s_rx_task_h = NULL;
    }
}

/* ---- TX: host -> controller ---- */
int
ble_transport_to_ll_cmd_impl(void *buf)
{
    uint8_t *p = buf;
    uint16_t len = 3 + p[2];   /* opcode(2) + param_len(1) + params */
    uint16_t i;

    hci_ctrl_putc(HCI_H4_CMD);
    for (i = 0; i < len; i++) {
        hci_ctrl_putc(p[i]);
    }
    ble_transport_free(buf);
    return 0;
}

int
ble_transport_to_ll_acl_impl(struct os_mbuf *om)
{
    uint16_t len = OS_MBUF_PKTLEN(om);
    uint16_t i;
    uint8_t  b;

    hci_ctrl_putc(HCI_H4_ACL);
    for (i = 0; i < len; i++) {
        os_mbuf_copydata(om, i, 1, &b);
        hci_ctrl_putc(b);
    }
    os_mbuf_free_chain(om);
    return 0;
}
