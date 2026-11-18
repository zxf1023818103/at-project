/* SPDX-License-Identifier: Apache-2.0 */
#include "FreeRTOS.h"
#include <stdint.h>
#include <string.h>

/* Bluedroid's realloc needs the old allocation size. A 16-byte prefix preserves
 * the port's alignment. Every allocation belongs to the FreeRTOS heap. */
void *tl3238_bd_malloc(size_t size)
{
    if (size > SIZE_MAX - 16u) return NULL;
    size_t *p = pvPortMalloc(size + 16u);
    if (!p) return NULL;
    *p = size;
    return (uint8_t *)p + 16u;
}

void tl3238_bd_free(void *p)
{
    if (p) vPortFree((uint8_t *)p - 16u);
}

void *tl3238_bd_calloc(size_t n, size_t size)
{
    if (size && n > SIZE_MAX / size) return NULL;
    void *p = tl3238_bd_malloc(n * size);
    if (p) memset(p, 0, n * size);
    return p;
}

void *tl3238_bd_realloc(void *p, size_t size)
{
    if (!size) { tl3238_bd_free(p); return NULL; }
    void *q = tl3238_bd_malloc(size);
    if (q && p) {
        size_t old = *(size_t *)((uint8_t *)p - 16u);
        memcpy(q, p, old < size ? old : size);
        tl3238_bd_free(p);
    }
    return q;
}

/* Remaining instruction/data RAM are separate, ordered regions. */
static uint8_t iram_heap[38 * 1024] __attribute__((section(".iram_bss"), aligned(16)));
static uint8_t dram_heap[38 * 1024] __attribute__((aligned(16)));
void tl3238_app_memory_init(void)
{
    const HeapRegion_t regions[] = {
        {iram_heap, sizeof(iram_heap)}, {dram_heap, sizeof(dram_heap)}, {NULL, 0}
    };
    configASSERT(sizeof(iram_heap) + sizeof(dram_heap) == configTOTAL_HEAP_SIZE);
    vPortDefineHeapRegions(regions);
}
