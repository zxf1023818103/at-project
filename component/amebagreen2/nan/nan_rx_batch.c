#include "nan_rx_batch.h"
#include "FreeRTOS_IP_Private.h"
#include "NetworkBufferManagement.h"

BaseType_t green2_nan_rx_batch_push(struct green2_nan_rx_batch *batch,
                                   NetworkBufferDescriptor_t *buffer)
{
    if (!buffer || batch->count == GREEN2_NAN_RX_BATCH_LIMIT) return pdFALSE;
    buffer->pxNextBuffer = NULL;
    if (batch->tail) batch->tail->pxNextBuffer = buffer;
    else batch->head = buffer;
    batch->tail = buffer;
    ++batch->count;
    return pdTRUE;
}

unsigned green2_nan_rx_batch_flush(struct green2_nan_rx_batch *batch,
                                  BaseType_t deliver)
{
    NetworkBufferDescriptor_t *head = batch->head;
    unsigned count = batch->count;
    if (!head) return 0;
    /* Sending can immediately wake the IP task, which may free every buffer
     * before xSendEventStructToIPTask returns. Clear producer state first. */
    batch->head = batch->tail = NULL;
    batch->count = 0;
    IPStackEvent_t event = {eNetworkRxEvent, head};
    if (deliver && xSendEventStructToIPTask(&event, 0) == pdPASS) return 0;
    while (head) {
        NetworkBufferDescriptor_t *next = head->pxNextBuffer;
        head->pxNextBuffer = NULL;
        vReleaseNetworkBufferAndDescriptor(head);
        head = next;
    }
    return count;
}
