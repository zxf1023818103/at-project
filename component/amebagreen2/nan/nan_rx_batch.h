#ifndef GREEN2_NAN_RX_BATCH_H
#define GREEN2_NAN_RX_BATCH_H
#include "FreeRTOS_IP.h"

#define GREEN2_NAN_RX_BATCH_LIMIT 8u

/* Single producer: the WHC receive task. The IP task owns a chain only after
 * a successful flush. No allocation or timer is needed to form a batch. */
struct green2_nan_rx_batch {
    NetworkBufferDescriptor_t *head, *tail;
    unsigned count;
};

/* On failure, ownership of buffer remains with the caller. */
BaseType_t green2_nan_rx_batch_push(struct green2_nan_rx_batch *batch,
                                   NetworkBufferDescriptor_t *buffer);
/* Empty the batch; return the number of dropped and freed buffers. */
unsigned green2_nan_rx_batch_flush(struct green2_nan_rx_batch *batch,
                                  BaseType_t deliver);
#endif
