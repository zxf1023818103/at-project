#include "nan_heap.h"
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "NetworkBufferManagement.h"
#include "mm.h"
#include <stdio.h>
#include <string.h>

void *bl618dg_nan_malloc(size_t size)
{
    vTaskSuspendAll();
    void *p = kmalloc(size, MM_FLAG_HEAP_PSRAM_0);
    traceMALLOC(p, size);
    (void)xTaskResumeAll();
    if (!p) {
        printf("[NAN] PSRAM allocation failed bytes=%lu task=%s\n",
               (unsigned long)size, pcTaskGetName(NULL));
        bl618dg_nan_heap_status();
        vApplicationMallocFailedHook();
    }
    return p;
}

void bl618dg_nan_heap_status(void)
{
    printf("[NAN] heap free/max OCRAM=%lu/%lu PSRAM=%lu/%lu WRAM=%lu/%lu\n",
        (unsigned long)kfree_size(MM_HEAP_OCRAM_0), (unsigned long)kmax_free_size(MM_HEAP_OCRAM_0),
        (unsigned long)kfree_size(MM_HEAP_PSRAM_0), (unsigned long)kmax_free_size(MM_HEAP_PSRAM_0),
        (unsigned long)kfree_size(MM_HEAP_WRAM_0), (unsigned long)kmax_free_size(MM_HEAP_WRAM_0));
}

/* Hold more data than the whole OCRAM heap can contain. Network buffers use
 * the real FreeRTOS+TCP allocator; the remaining blocks model simultaneous
 * bidirectional socket streams and a full USB data queue. No radio traffic. */
void bl618dg_nan_heap_selftest(void)
{
    enum { STREAMS = 2, PACKETS = 32, MESSAGES = 64, COUNT = STREAMS + PACKETS + MESSAGES };
    uint8_t *blocks[COUNT];
    size_t lengths[COUNT];
    size_t before = kfree_size(MM_HEAP_PSRAM_0);
    for (unsigned round = 0; round < 8; ++round) {
        for (unsigned i = 0; i < COUNT; ++i) {
            int packet = i >= STREAMS && i < STREAMS + PACKETS;
            lengths[i] = i < STREAMS ? 65536 : (packet ? 1514 : 1530);
            blocks[i] = packet ? pucGetNetworkBuffer(&lengths[i]) : pvPortMalloc(lengths[i]);
            configASSERT(blocks[i]);
            mm_heap_t *heap = mm_find_heap_by_addr(blocks[i]);
            configASSERT(heap && heap->heap_id == MM_HEAP_PSRAM_0);
            memset(blocks[i], (uint8_t)(i + round), lengths[i]);
        }
        for (unsigned i = 0; i < COUNT; ++i)
            for (size_t j = 0; j < lengths[i]; ++j)
                configASSERT(blocks[i][j] == (uint8_t)(i + round));
        /* Free in a different order to exercise coalescing between bursts. */
        for (unsigned parity = 0; parity < 2; ++parity) {
            for (unsigned i = parity; i < COUNT; i += 2) {
                if (i >= STREAMS && i < STREAMS + PACKETS) vReleaseNetworkBuffer(blocks[i]);
                else vPortFree(blocks[i]);
            }
        }
    }
    configASSERT(kfree_size(MM_HEAP_PSRAM_0) == before);
    puts("[NAN] PSRAM allocation stress PASS: 8 rounds, 2x64KiB + 32 network buffers + 64 MTU blocks");
    bl618dg_nan_heap_status();
}
