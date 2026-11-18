#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_bluedroid_hci.h"
#include "hci_onchip.h"
#if defined(CONFIG_PAN_GATT_BR) || defined(CONFIG_PAN_GATT_LE)
#include "bflb_mtimer.h"
static uint16_t timing_rx_handle;
static volatile uint32_t timing_arrival;
static volatile bool timing_pending;
static uint32_t timing_count, timing_submit_sum, timing_submit_max, timing_done_sum, timing_done_max;
void bluedroid_bouffalo_gatt_timing_reset(unsigned short rx_handle)
{
    taskENTER_CRITICAL();
    timing_rx_handle = rx_handle; timing_pending = false;
    timing_count = timing_submit_sum = timing_submit_max = timing_done_sum = timing_done_max = 0;
    taskEXIT_CRITICAL();
}
void bluedroid_bouffalo_gatt_timing_report(void)
{
    taskENTER_CRITICAL();
    uint32_t count=timing_count, submit=timing_submit_sum, submit_max=timing_submit_max;
    uint32_t done=timing_done_sum, done_max=timing_done_max;
    taskEXIT_CRITICAL();
    printf("[gatt-br-hci-time] count=%lu rx_to_submit_avg_us=%lu max_us=%lu rx_to_done_avg_us=%lu max_us=%lu\r\n",
           (unsigned long)count, (unsigned long)(count ? submit/count : 0), (unsigned long)submit_max,
           (unsigned long)(count ? done/count : 0), (unsigned long)done_max);
}
#endif

/* The transport owns only queued TX copies. Host/controller HCI credit
 * accounting remains in Bluedroid and the controller, respectively. */
#define HCI_PACKET_MAX 1026
struct tx_packet { uint16_t length; uint8_t bytes[]; };
static QueueHandle_t tx_queue;
static esp_bluedroid_hci_driver_callbacks_t host_callbacks;

void bluedroid_bouffalo_use_combo_rf(void)
{
#if defined(BL618DG)
    /* Matches the vendor's current BL618DG btble_cli bring-up. The older SDK
     * example omits this call even though its PhyRF archive already exports it. */
    extern void cmd_set_btble_combo(int argc, char **argv);
    cmd_set_btble_combo(0, NULL);
#endif
}


bool bluedroid_bouffalo_classic_available(void)
{
#if defined(BL616)
    extern int btble_bt_isEnabled(void);
    return btble_bt_isEnabled() != 0;
#else
    return true;
#endif
}

static uint16_t le16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
#if defined(CONFIG_PAN_GATT_BR) || defined(CONFIG_PAN_GATT_LE)
/* Control PDUs only: never log each bulk notification or Write Command. */
static void trace_att_control(const char *direction, const uint8_t *p, uint16_t length)
{
    if (length < 10 || (le16(p+1)&0x3000)==0x1000) return;
    uint16_t cid=le16(p+7);
    uint8_t op=p[9];
    if ((cid!=4 && cid<0x40) || op==0x1b || op==0x52 || op>0x23) return;
    printf("[att-control] ms=%lu %s cid=%04x op=%02x len=%u data=",
           (unsigned long)(bflb_mtimer_get_time_us()/1000),direction,cid,op,le16(p+5));
    for (unsigned i=9; i<length && i<25; ++i) printf("%02x",p[i]);
    printf("\r\n");
}
#endif


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
#ifdef CONFIG_PAN_GATT_LE
        if (type == BT_HCI_LE_EVT && size) {
            if (param[0] == 0x0c && size >= 6)
                printf("[gatt-le-hci] PHY status=%u tx=%u rx=%u\r\n",param[1],param[4],param[5]);
            else if (param[0] == 0x07 && size >= 11)
                printf("[gatt-le-hci] DLE tx_octets=%u tx_time=%u rx_octets=%u rx_time=%u\r\n",le16(param+3),le16(param+5),le16(param+7),le16(param+9));
            else if (param[0] == 0x03 && size >= 10)
                printf("[gatt-le-hci] UPDATE status=%u interval=%u latency=%u timeout=%u\r\n",param[1],le16(param+4),le16(param+6),le16(param+8));
            else if (param[0] == 0x01 && size >= 19)
                printf("[gatt-le-hci] CONNECT status=%u interval=%u latency=%u timeout=%u\r\n",param[1],le16(param+12),le16(param+14),le16(param+16));
            else if (param[0] == 0x0a && size >= 31)
                printf("[gatt-le-hci] CONNECT status=%u interval=%u latency=%u timeout=%u\r\n",param[1],le16(param+24),le16(param+26),le16(param+28));
        }
#endif
        break;
    case BT_HCI_ACL_DATA:
        packet[0] = 2;
        length = bt_onchiphci_handle_rx_acl(param, packet + 1) + 1;
        configASSERT(length <= HCI_PACKET_MAX);
#if defined(CONFIG_PAN_GATT_BR) || defined(CONFIG_PAN_GATT_LE)
        trace_att_control("RX",packet,length);
#endif
#if defined(CONFIG_PAN_GATT_BR) || defined(CONFIG_PAN_GATT_LE)
        if (length >= 13 && (le16(packet+1)&0x3000)==0x2000 && packet[9]==0x12 && le16(packet+10)==timing_rx_handle) {
            timing_arrival = (uint32_t)bflb_mtimer_get_time_us();
            timing_pending = true;
        }
#endif
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
#if defined(CONFIG_PAN_GATT_BR) || defined(CONFIG_PAN_GATT_LE)
        bool timed_response = type==BT_HCI_ACL_DATA && tx->length>=10 &&
                              (le16(p+1)&0x3000)!=0x1000 && p[9]==0x13 && timing_pending;
        uint32_t arrival=timing_arrival;
        uint32_t submit=timed_response ? (uint32_t)bflb_mtimer_get_time_us()-arrival : 0;
        if (timed_response) timing_pending=false;
#endif
#if defined(CONFIG_PAN_GATT_BR) || defined(CONFIG_PAN_GATT_LE)
        if (type==BT_HCI_ACL_DATA) trace_att_control("TX",p,tx->length);
#endif
        int result = bt_onchiphci_send(type, destination, &packet);
#if defined(CONFIG_PAN_GATT_BR) || defined(CONFIG_PAN_GATT_LE)
        if (timed_response) {
            uint32_t done=(uint32_t)bflb_mtimer_get_time_us()-arrival;
            timing_count++;
            timing_submit_sum+=submit; timing_done_sum+=done;
            if(submit>timing_submit_max) timing_submit_max=submit;
            if(done>timing_done_max) timing_done_max=done;
        }
#endif
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
