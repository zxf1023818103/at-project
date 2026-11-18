#undef NDEBUG
#include <assert.h>
#include <stdlib.h>
#include "nan_rx_batch.h"
#include "FreeRTOS_IP_Private.h"
#include "NetworkBufferManagement.h"

static struct green2_nan_rx_batch batch;
static unsigned sends, freed, order[32];
static BaseType_t queue_accepts = pdTRUE;

void vReleaseNetworkBufferAndDescriptor(NetworkBufferDescriptor_t *buffer)
{
    assert(buffer->pxNextBuffer == NULL);
    assert(freed < sizeof(order) / sizeof(order[0]));
    order[freed++] = buffer->test_id;
    free(buffer);
}

BaseType_t xSendEventStructToIPTask(const IPStackEvent_t *event, TickType_t wait)
{
    assert(wait == 0 && event->eEventType == eNetworkRxEvent);
    /* Model an immediate preemption by the higher-priority IP task. */
    assert(batch.count == 0 && !batch.head && !batch.tail);
    ++sends;
    if (!queue_accepts) return pdFAIL;
    NetworkBufferDescriptor_t *head = event->pvData;
    while (head) {
        NetworkBufferDescriptor_t *next = head->pxNextBuffer;
        head->pxNextBuffer = NULL;
        vReleaseNetworkBufferAndDescriptor(head);
        head = next;
    }
    return pdPASS;
}

static NetworkBufferDescriptor_t *packet(unsigned id)
{
    NetworkBufferDescriptor_t *p = calloc(1, sizeof(*p));
    assert(p);
    p->test_id = id;
    return p;
}

int main(void)
{
    assert(green2_nan_rx_batch_flush(&batch, pdTRUE) == 0 && sends == 0);
    assert(!green2_nan_rx_batch_push(&batch, NULL));
    for (unsigned i = 0; i < GREEN2_NAN_RX_BATCH_LIMIT; ++i)
        assert(green2_nan_rx_batch_push(&batch, packet(i)));
    NetworkBufferDescriptor_t *overflow = packet(8);
    assert(!green2_nan_rx_batch_push(&batch, overflow));
    assert(batch.count == GREEN2_NAN_RX_BATCH_LIMIT && !batch.tail->pxNextBuffer);
    assert(green2_nan_rx_batch_flush(&batch, pdTRUE) == 0);
    assert(sends == 1 && freed == 8);
    for (unsigned i = 0; i < 8; ++i) assert(order[i] == i);
    /* A rejected push must leave ownership with its caller. */
    vReleaseNetworkBufferAndDescriptor(overflow);

    /* Queue exhaustion must release the entire unsent chain, once each. */
    queue_accepts = pdFALSE;
    for (unsigned i = 9; i < 12; ++i)
        assert(green2_nan_rx_batch_push(&batch, packet(i)));
    assert(green2_nan_rx_batch_flush(&batch, pdTRUE) == 3);
    assert(sends == 2 && freed == 12 && !batch.head && !batch.tail);
    for (unsigned i = 0; i < 12; ++i) assert(order[i] == i);

    /* An invalidated NDP generation is discarded without contacting IP. */
    assert(green2_nan_rx_batch_push(&batch, packet(12)));
    assert(green2_nan_rx_batch_push(&batch, packet(13)));
    assert(green2_nan_rx_batch_flush(&batch, pdFALSE) == 2);
    assert(sends == 2 && freed == 14);

    /* A new one-packet batch can be flushed immediately after either error. */
    queue_accepts = pdTRUE;
    assert(green2_nan_rx_batch_push(&batch, packet(14)));
    assert(green2_nan_rx_batch_flush(&batch, pdTRUE) == 0);
    assert(sends == 3 && freed == 15 && order[14] == 14);
    assert(green2_nan_rx_batch_flush(&batch, pdFALSE) == 0);
    return 0;
}
