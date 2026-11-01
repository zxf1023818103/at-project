#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_bluedroid_hci.h"
#include "hci_onchip.h"

/* The transport owns only queued TX copies. Host/controller HCI credit
 * accounting remains in Bluedroid and the controller, respectively. */
#define HCI_PACKET_MAX 1026
struct tx_packet { uint16_t length; uint8_t bytes[]; };
static QueueHandle_t tx_queue;
static esp_bluedroid_hci_driver_callbacks_t host_callbacks;

void bluedroid_bouffalo_use_combo_rf(void)
{
    /* Matches the vendor's current BL618DG btble_cli bring-up. The older SDK
     * example omits this call even though its PhyRF archive already exports it. */
    extern void cmd_set_btble_combo(int argc, char **argv);
    cmd_set_btble_combo(0, NULL);
}


static uint16_t le16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }

static void receive_packet(uint8_t type, uint16_t id, uint8_t *param, uint8_t size)
{
    /* Current B0 controller's Read Buffer Size reports 1021-byte ACL payloads.
     * Leave additional room for the vendor ACL extraction helper. It consumes
     * its opaque controller buffer and produces an HCI ACL header + payload. */
    uint8_t packet[2048];
    uint16_t length;
    if (!host_callbacks.notify_host_recv) return;
#if CONFIG_PAN_HCI_TRACE
    if (type != BT_HCI_ACL_DATA)
        printf("[hci] rx type=%u id=%04x size=%u status=%02x\r\n", type, id, size,
               (type == BT_HCI_CMD_CMP_EVT || type == BT_HCI_CMD_STAT_EVT) && size ? param[0] : 0);
#endif
    packet[0] = 4; /* H:4 event */
    switch (type) {
    case BT_HCI_CMD_CMP_EVT:
        configASSERT(size <= 252);
        packet[1] = 0x0e;
        packet[2] = size + 3;
        packet[3] = 1;
        packet[4] = id;
        packet[5] = id >> 8;
        memcpy(packet + 6, param, size);
        length = size + 6;
        break;
    case BT_HCI_CMD_STAT_EVT:
        configASSERT(size >= 1);
        packet[1] = 0x0f;
        packet[2] = 4;
        packet[3] = param[0];
        packet[4] = 1;
        packet[5] = id;
        packet[6] = id >> 8;
        length = 7;
        break;
    case BT_HCI_EVT:
    case BT_HCI_LE_EVT:
    case BT_HCI_DBG_EVT:
        packet[1] = type == BT_HCI_LE_EVT ? 0x3e : type == BT_HCI_DBG_EVT ? 0xff : id;
        packet[2] = size;
        memcpy(packet + 3, param, size);
        length = size + 3;
        break;
    case BT_HCI_ACL_DATA:
        packet[0] = 2;
        length = bt_onchiphci_handle_rx_acl(param, packet + 1) + 1;
        configASSERT(length <= HCI_PACKET_MAX);
#if CONFIG_PAN_HCI_TRACE
        printf("[hci] rx ACL handle_flags=%04x length=%u", le16(packet + 1), length - 5);
        if ((packet[2] & 0x30) != 0x10 && length >= 11)
            printf(" cid=%04x code=%02x ident=%02x", le16(packet + 7), packet[9], packet[10]);
        printf("\r\n");
#endif
        break;
    default:
        /* PAN uses ACL only. SCO/ISO need their own transport implementation. */
        printf("[bluedroid] unsupported controller packet %u\r\n", type);
        return;
    }
    configASSERT(host_callbacks.notify_host_recv(packet, length) == 0);
}

static void transmit_worker(void *unused)
{
    (void)unused;
    struct tx_packet *tx;
    for (;;) {
        if (xQueueReceive(tx_queue, &tx, portMAX_DELAY) != pdPASS) continue;
        uint8_t *p = tx->bytes;
        hci_pkt_struct packet = {0};
        uint8_t type;
        uint16_t destination = 0;
        if (p[0] == 1) {
            configASSERT(tx->length >= 4 && tx->length == p[3] + 4);
            type = BT_HCI_CMD;
            packet.p.hci_cmd.opcode = le16(p + 1);
#if CONFIG_PAN_HCI_TRACE
            printf("[hci] tx command=%04x\r\n", packet.p.hci_cmd.opcode);
#endif
            packet.p.hci_cmd.param_len = p[3];
            packet.p.hci_cmd.params = p + 4;
        } else {
            configASSERT(p[0] == 2 && tx->length >= 5);
            configASSERT(tx->length == le16(p + 3) + 5);
            type = BT_HCI_ACL_DATA;
            uint16_t handle_flags = le16(p + 1);
            destination = handle_flags & 0x0fff;
            packet.p.acl_data.conhdl = destination;
            packet.p.acl_data.pb_bc_flag = handle_flags >> 12;
            packet.p.acl_data.len = le16(p + 3);
            packet.p.acl_data.buffer = p + 5;
#if CONFIG_PAN_HCI_TRACE
            printf("[hci] tx ACL handle_flags=%04x length=%u", handle_flags, packet.p.acl_data.len);
            if ((handle_flags & 0x3000) != 0x1000 && tx->length >= 11)
                printf(" cid=%04x code=%02x ident=%02x", le16(p + 7), p[9], p[10]);
            printf("\r\n");
#endif
        }
        int result = bt_onchiphci_send(type, destination, &packet);
        if (result) printf("[bluedroid] controller send error=%d\r\n", result);
        configASSERT(result == 0);
        free(tx);
        if (host_callbacks.notify_host_send_available)
            host_callbacks.notify_host_send_available();
    }
}

static void send_packet(uint8_t *data, uint16_t length)
{
    configASSERT(data && length && length <= HCI_PACKET_MAX);
    struct tx_packet *tx = malloc(sizeof(*tx) + length);
    configASSERT(tx);
    tx->length = length;
    memcpy(tx->bytes, data, length);
    configASSERT(xQueueSend(tx_queue, &tx, portMAX_DELAY) == pdPASS);
}

static bool send_available(void) { return tx_queue && uxQueueSpacesAvailable(tx_queue); }

static esp_err_t register_callbacks(const esp_bluedroid_hci_driver_callbacks_t *callbacks)
{
    if (!callbacks) return ESP_ERR_INVALID_ARG;
    host_callbacks = *callbacks;
    return ESP_OK;
}

esp_err_t bluedroid_bouffalo_hci_attach(void)
{
    if (tx_queue) return ESP_ERR_INVALID_STATE;
    tx_queue = xQueueCreate(8, sizeof(struct tx_packet *));
    if (!tx_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(transmit_worker, "bd_tx", 1024, NULL,
                    configMAX_PRIORITIES - 2, NULL) != pdPASS) {
        vQueueDelete(tx_queue);
        tx_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    if (bt_onchiphci_interface_init(receive_packet)) return ESP_FAIL;
    const esp_bluedroid_hci_driver_operations_t driver = {
        .send = send_packet,
        .check_send_available = send_available,
        .register_host_callback = register_callbacks,
    };
    return esp_bluedroid_attach_hci_driver(&driver);
}
