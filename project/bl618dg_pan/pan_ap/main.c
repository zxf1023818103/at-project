#include <stdio.h>
#include <string.h>
#include "board.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "btble_lib_api.h"
#include "hci_onchip.h"
#include "rfparam_adapter.h"

/* Controller callbacks run in its task. Copy events before returning: the
 * controller owns the parameter memory. Only one command is outstanding. */
struct hci_event {
    uint8_t type;
    uint16_t id;
    uint8_t length;
    uint8_t data[255];
};
static QueueHandle_t events;
static unsigned event_drops;

static void receive_event(uint8_t type, uint16_t id, uint8_t *data, uint8_t length)
{
    struct hci_event event = {.type = type, .id = id, .length = length};
    if (type == BT_HCI_ACL_DATA) {
        /* The smoke image never creates ACL connections. */
        ++event_drops;
        return;
    }
    if (length) memcpy(event.data, data, length);
    if (xQueueSend(events, &event, 0) != pdPASS) ++event_drops;
}

static int command(uint16_t opcode, const void *parameters, uint8_t length)
{
    hci_pkt_struct packet = {0};
    struct hci_event event;
    packet.p.hci_cmd.opcode = opcode;
    packet.p.hci_cmd.params = (uint8_t *)parameters;
    packet.p.hci_cmd.param_len = length;
    printf("[hci] tx %04x\r\n", opcode);
    if (bt_onchiphci_send(BT_HCI_CMD, 0, &packet)) return -1;
    TickType_t start = xTaskGetTickCount();
    while (xTaskGetTickCount() - start < pdMS_TO_TICKS(3000)) {
        if (xQueueReceive(events, &event, pdMS_TO_TICKS(100)) != pdPASS) continue;
        printf("[hci] rx type=%u id=%04x len=%u:", event.type, event.id, event.length);
        for (unsigned i = 0; i < event.length; ++i) printf(" %02x", event.data[i]);
        printf("\r\n");
        if (event.type == BT_HCI_CMD_CMP_EVT && event.id == opcode)
            return event.length ? event.data[0] : -1;
        if (event.type == BT_HCI_CMD_STAT_EVT && event.id == opcode)
            return event.length ? event.data[0] : -1;
    }
    printf("[hci] timeout %04x drops=%u\r\n", opcode, event_drops);
    return -1;
}

static void controller_smoke(void *unused)
{
    (void)unused;
    events = xQueueCreate(8, sizeof(struct hci_event));
    configASSERT(events);
    if (rfparam_init(0, NULL, 0)) {
        printf("[pan] RF init failed\r\n");
        vTaskDelete(NULL);
    }
    printf("[pan] starting B0 dual-mode controller\r\n");
    btble_controller_init(configMAX_PRIORITIES - 1);
    int result = bt_onchiphci_interface_init(receive_event);
    printf("[pan] HCI interface=%d\r\n", result);
    static const uint16_t queries[] = {0x0c03, 0x1001, 0x1002, 0x1003, 0x1005, 0x1009};
    for (unsigned i = 0; !result && i < sizeof(queries) / sizeof(queries[0]); ++i)
        result = command(queries[i], NULL, 0);
    printf("[pan] controller smoke %s result=%d drops=%u\r\n",
           result ? "FAIL" : "PASS", result, event_drops);
    vTaskDelete(NULL);
}

int main(void)
{
    board_init();
    printf("[pan] BL618DG B0 PAN bring-up\r\n");
    configASSERT(xTaskCreate(controller_smoke, "pan_smoke", 2048, NULL,
                            configMAX_PRIORITIES - 2, NULL) == pdPASS);
    vTaskStartScheduler();
    for (;;) {}
}
