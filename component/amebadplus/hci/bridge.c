/* Transparent H4 bridge on the existing LOGUART after controller calibration.
 * The host owns command flow; no NimBLE host or CLI consumes the same UART. */
#include "ameba_soc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "wifi_service.h"
#include "console.h"
#include "hci_controller.h"
#include "hci_transport.h"
#include "rtk_bt_gap.h"
#include <stdio.h>

static void discard(char c) { (void)c; }
static int no_input(void) { return -1; }
static const at_console_backend_t silent = {discard, no_input, discard};
uint16_t rtk_bt_gap_vendor_cmd_req(rtk_bt_gap_vendor_cmd_param_t *param)
{
    (void)param;
    /* A second command producer would corrupt the external host's HCI credits. */
    return 1;
}
static void receive(struct hci_rx_packet_t *packet)
{
    /* ROM PutChar adds CR after LF, corrupting a binary H4 length/payload. */
    extern void LOGUART_PutChar_RAM(uint8_t c);
    LOGUART_PutChar_RAM(packet->type);
    for (unsigned i = 0; i < packet->len; ++i) LOGUART_PutChar_RAM(packet->buf[i]);
    hci_rx_pkt_free(packet);
}
static int get_bytes(uint8_t *p, unsigned n)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(2000);
    while (n) {
        if (LOGUART_Readable()) {
            *p++ = (uint8_t)DiagGetChar(0); --n;
            deadline = xTaskGetTickCount() + pdMS_TO_TICKS(2000);
        } else {
            if ((int32_t)(xTaskGetTickCount() - deadline) >= 0) return -1;
            vTaskDelay(1);
        }
    }
    return 0;
}
static void bridge_task(void *arg)
{
    (void)arg;
    extern int amebadplus_wifi_ready(void);
    extern void amebadplus_console_claim(void);
    static struct hci_transport_cb callback = {.recv = receive};
    static uint8_t packet[4096];
    while (!amebadplus_wifi_ready()) vTaskDelay(pdMS_TO_TICKS(50));
    if (wifi_service_pause(15000) || !hci_controller_open()) {
        printf("[HCI] controller/association ownership failed\n"); vTaskDelete(NULL);
    }
    amebadplus_console_claim();
    printf("[HCI] entering HCI bridge at 1500000 baud\n");
    vTaskDelay(pdMS_TO_TICKS(30));
    at_console_set_backend(&silent);
    rtk_log_level_set("*", RTK_LOG_NONE);
    hci_transport_register(&callback);
    for (;;) {
        uint8_t type;
        if (get_bytes(&type, 1)) continue;
        unsigned hdr = type == HCI_CMD || type == HCI_SCO ? 3 : type == HCI_ACL ? 4 : 0;
        if (!hdr || get_bytes(packet, hdr)) continue;
        unsigned payload = type == HCI_ACL ? packet[2] | packet[3] << 8 : packet[2];
        if (payload > sizeof(packet) - hdr) {
            while (payload--) { uint8_t ignored; if (get_bytes(&ignored, 1)) break; }
            continue;
        }
        if (!get_bytes(packet + hdr, payload))
            (void)hci_transport_send(type, packet, hdr + payload, false);
    }
}
void amebadplus_hci_start(void)
{
    configASSERT(xTaskCreate(bridge_task, "hci_bridge", 3072, NULL, 2, NULL) == pdPASS);
}
