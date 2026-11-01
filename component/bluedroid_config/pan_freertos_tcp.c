#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "event_groups.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_IP_Private.h"
#include "FreeRTOS_Routing.h"
#include "NetworkBufferManagement.h"
#include "bluedroid_pan_tcp.h"

#define CAN_SEND (1U << 0)
#define WRITE_DONE (1U << 1)
#define LINK_CHANGED (1U << 2)
struct output_packet { NetworkBufferDescriptor_t *buffer; uint32_t generation; };
static NetworkInterface_t interface;
static NetworkEndPoint_t endpoint;
static QueueHandle_t output_queue;
static EventGroupHandle_t signals;
static volatile BaseType_t link_up;
static volatile uint16_t pan_handle = ESP_PAN_INVALID_HANDLE;
static volatile uint32_t generation;
static volatile esp_pan_status_t write_status;
static unsigned rx_packets, tx_packets, rx_drops, tx_drops;

static BaseType_t initialise(NetworkInterface_t *unused)
{ (void)unused; return link_up ? pdPASS : pdFAIL; }
static BaseType_t link_status(NetworkInterface_t *unused)
{ (void)unused; return link_up; }

static BaseType_t output(NetworkInterface_t *unused, NetworkBufferDescriptor_t *buffer,
                         BaseType_t release)
{
    (void)unused;
    NetworkBufferDescriptor_t *queued = buffer;
    BaseType_t result = pdFAIL;
    taskENTER_CRITICAL();
    uint32_t queued_generation = generation;
    BaseType_t connected = link_up;
    taskEXIT_CRITICAL();
    if (!connected || buffer->xDataLength < 14 || buffer->xDataLength > 1514) goto done;
    if (!release) {
        queued = pxGetNetworkBufferWithDescriptor(buffer->xDataLength, 0);
        if (!queued) goto done;
        memcpy(queued->pucEthernetBuffer, buffer->pucEthernetBuffer, buffer->xDataLength);
        queued->xDataLength = buffer->xDataLength;
    }
    struct output_packet packet = {.buffer = queued, .generation = queued_generation};
    result = xQueueSend(output_queue, &packet, 0);
    if (result != pdPASS && !release) vReleaseNetworkBufferAndDescriptor(queued);
done:
    if (result != pdPASS) {
        ++tx_drops;
        if (release) vReleaseNetworkBufferAndDescriptor(buffer);
    }
    return result;
}

static void output_task(void *unused)
{
    (void)unused;
    struct output_packet packet;
    for (;;) {
        if (xQueueReceive(output_queue, &packet, portMAX_DELAY) != pdPASS) continue;
        xEventGroupWaitBits(signals, CAN_SEND, pdFALSE, pdTRUE, pdMS_TO_TICKS(1000));
        if (!link_up || packet.generation != generation ||
            !(xEventGroupGetBits(signals) & CAN_SEND)) {
            ++tx_drops;
            vReleaseNetworkBufferAndDescriptor(packet.buffer);
            continue;
        }
        uint16_t handle = pan_handle;
        uint8_t *frame = packet.buffer->pucEthernetBuffer;
        uint16_t type = (uint16_t)frame[12] << 8 | frame[13];
        xEventGroupClearBits(signals, WRITE_DONE | LINK_CHANGED);
        esp_err_t result = esp_pan_write(handle, frame, frame + 6, type,
                                         packet.buffer->xDataLength - 14, frame + 14, false);
        /* esp_pan_write deep-copies the payload before returning. */
        vReleaseNetworkBufferAndDescriptor(packet.buffer);
        if (result != ESP_OK) { ++tx_drops; continue; }
        EventBits_t bits = xEventGroupWaitBits(signals, WRITE_DONE | LINK_CHANGED,
                                              pdTRUE, pdFALSE, pdMS_TO_TICKS(5000));
        if (packet.generation != generation) { ++tx_drops; continue; }
        if (!(bits & WRITE_DONE)) {
            /* A late completion cannot be matched to a subsequent frame. End
             * this connection rather than misattribute completion statuses. */
            ++tx_drops;
            link_up = pdFALSE;
            FreeRTOS_NetworkDown(&interface);
            esp_pan_disconnect(handle);
        } else if (write_status == ESP_PAN_SUCCESS) {
            ++tx_packets;
        } else {
            ++tx_drops;
        }
    }
}

NetworkInterface_t *bluedroid_pan_tcp_register(const uint8_t mac[6])
{
    if (output_queue || !mac) return NULL;
    output_queue = xQueueCreate(6, sizeof(struct output_packet));
    signals = xEventGroupCreate();
    configASSERT(output_queue && signals);
    configASSERT(xTaskCreate(output_task, "pan_tx", 1024, NULL,
                            configMAX_PRIORITIES - 3, NULL) == pdPASS);
    interface.pcName = "pan0";
    interface.pfInitialise = initialise;
    interface.pfOutput = output;
    interface.pfGetPhyLinkStatus = link_status;
    FreeRTOS_AddNetworkInterface(&interface);
    const uint8_t zero[4] = {0};
    const uint8_t mask[4] = {255, 255, 255, 255};
    /* A dormant /0 endpoint otherwise captures another interface's routes. */
    FreeRTOS_FillEndPoint(&interface, &endpoint, zero, mask, zero, zero, mac);
    endpoint.bits.bWantDHCP = pdTRUE;
    return &interface;
}

void bluedroid_pan_tcp_event(esp_pan_cb_event_t event, esp_pan_cb_param_t *param)
{
    if (!output_queue || !param) return;
    switch (event) {
    case ESP_PAN_OPEN_EVT:
        if (param->open.status != ESP_PAN_SUCCESS) break;
        ++generation;
        pan_handle = param->open.handle;
        link_up = pdTRUE;
        xEventGroupSetBits(signals, CAN_SEND);
        FreeRTOS_NetworkDown(&interface); /* Reinitialise and start DHCP. */
        break;
    case ESP_PAN_CLOSE_EVT:
        if (param->close.handle != pan_handle) break;
        link_up = pdFALSE;
        pan_handle = ESP_PAN_INVALID_HANDLE;
        ++generation;
        xEventGroupSetBits(signals, CAN_SEND | LINK_CHANGED);
        FreeRTOS_NetworkDown(&interface);
        break;
    case ESP_PAN_CONG_EVT:
        if (param->cong.handle != pan_handle) break;
        if (param->cong.cong) xEventGroupClearBits(signals, CAN_SEND);
        else xEventGroupSetBits(signals, CAN_SEND);
        break;
    case ESP_PAN_WRITE_EVT:
        if (param->write.handle != pan_handle) break;
        write_status = param->write.status;
        xEventGroupSetBits(signals, WRITE_DONE);
        break;
    case ESP_PAN_DATA_IND_EVT: {
        if (!link_up || param->data_ind.handle != pan_handle ||
            !param->data_ind.data || !param->data_ind.len || param->data_ind.len > 1500) {
            ++rx_drops;
            break;
        }
        NetworkBufferDescriptor_t *buffer = pxGetNetworkBufferWithDescriptor(param->data_ind.len + 14, 0);
        if (!buffer) { ++rx_drops; break; }
        uint8_t *frame = buffer->pucEthernetBuffer;
        memcpy(frame, param->data_ind.dst, 6);
        memcpy(frame + 6, param->data_ind.src, 6);
        frame[12] = param->data_ind.protocol >> 8;
        frame[13] = param->data_ind.protocol;
        memcpy(frame + 14, param->data_ind.data, param->data_ind.len);
        buffer->xDataLength = param->data_ind.len + 14;
        buffer->pxInterface = &interface;
        buffer->pxEndPoint = &endpoint;
        IPStackEvent_t message = {.eEventType = eNetworkRxEvent, .pvData = buffer};
        if (xSendEventStructToIPTask(&message, 0) != pdPASS) {
            vReleaseNetworkBufferAndDescriptor(buffer);
            ++rx_drops;
        } else ++rx_packets;
        break;
    }
    default: break;
    }
}

void bluedroid_pan_tcp_status(void)
{
    printf("[pan] link=%ld handle=%u rx=%u tx=%u rx_drop=%u tx_drop=%u\r\n",
           (long)link_up, pan_handle, rx_packets, tx_packets, rx_drops, tx_drops);
}
