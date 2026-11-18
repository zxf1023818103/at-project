/* SPDX-License-Identifier: Apache-2.0 */
#include "FreeRTOS.h"
#include "task.h"
#include "controller.h"
#include "esp_bluedroid_hci.h"
#include "tl3238_bluedroid.h"

static esp_bluedroid_hci_driver_callbacks_t host;

int tl3238_host_receive(const uint8_t *h4, size_t length)
{
    if (!host.notify_host_recv) return -1;
    configASSERT(length <= UINT16_MAX);
    /* The Bluedroid HAL copies before returning; the LL retains the FIFO. */
    if (length >= 7 && h4[0] == 4 && h4[1] == 0x0e)
        tl3238_bd_printf("[BD-HCI] CC opcode=%02x%02x status=%02x credits=%u\r\n", h4[5], h4[4], h4[6], h4[3]);
    else if (length >= 7 && h4[0] == 4 && h4[1] == 0x0f)
        tl3238_bd_printf("[BD-HCI] CS opcode=%02x%02x status=%02x credits=%u\r\n", h4[6], h4[5], h4[3], h4[4]);
    return host.notify_host_recv((uint8_t *)h4, length);
}

static bool available(void) { return tl3238_controller_stats.ready != 0; }

static void send_packet(uint8_t *data, uint16_t length)
{
    configASSERT(data && length >= 4 && (data[0] == 1 || data[0] == 2));
    if (data[0] == 1)
        tl3238_bd_printf("[BD-HCI] TX opcode=%02x%02x\r\n", data[2], data[1]);
    /* A single HCI producer. The highest-priority LL drains the bounded FIFO;
     * wait here rather than dropping a packet in the driver's void API. */
    TickType_t start = xTaskGetTickCount();
    while (!tl3238_controller_can_send()) {
        configASSERT(xTaskGetTickCount() - start < pdMS_TO_TICKS(1000));
        vTaskDelay(1);
    }
    configASSERT(tl3238_controller_send(data[0], data + 1, length - 1) == 0);
}

static esp_err_t register_callbacks(const esp_bluedroid_hci_driver_callbacks_t *cb)
{
    if (!cb) return ESP_ERR_INVALID_ARG;
    host = *cb;
    return ESP_OK;
}

esp_err_t tl3238_bluedroid_hci_attach(void)
{
    tl3238_controller_start();
    const esp_bluedroid_hci_driver_operations_t operations = {
        .send = send_packet, .check_send_available = available,
        .register_host_callback = register_callbacks,
    };
    return esp_bluedroid_attach_hci_driver(&operations);
}
