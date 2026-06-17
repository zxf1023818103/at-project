/*
 * freertos_heap_tlsf.c — AmebaD FreeRTOS heap backed by the shared tlsf_config
 * allocator (component/tlsf_config, TLSF), replacing the kernel's heap_5.c.
 *
 * The kernel's heap source is disabled (FREERTOS_HEAP points at the empty
 * freertos_heap_none.c), so this file provides the FreeRTOS heap surface
 * (pvPortMalloc/vPortFree/...) over kmalloc/kfree. The existing
 * freertos_heap5_config.c is kept UNCHANGED: its os_heap_init() still computes the
 * per-core AmebaD heap region table and calls vPortDefineHeapRegions() — which we
 * reimplement here to register each region as a tlsf_config TLSF heap. So the
 * battle-tested per-core region math (KM4 dynamic SRAM; KM0 static/ROM-BSS) is
 * reused verbatim, and only the allocator underneath changes.
 *
 * Also provides a STRONG mm_lock_save/mm_unlock_restore (tlsf_config's are weak): a
 * pre-scheduler-safe critical section via the Cortex-M PRIMASK.
 */
#include <stdint.h>
#include <stddef.h>
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include "mm.h"          /* shared tlsf_config: mem_manager_init / mm_register_heap / kmalloc / ... */

/* ---- tlsf_config critical section (strong override of the weak defaults) ---- *
 * Used before the scheduler starts (first allocations are C ctors / stdio), so it
 * uses the FreeRTOS interrupt-mask save/restore (safe in task + ISR + pre-scheduler
 * context) rather than a mutex. No CMSIS include needed. */
uintptr_t mm_lock_save(void)
{
    return (uintptr_t)portSET_INTERRUPT_MASK_FROM_ISR();
}

void mm_unlock_restore(uintptr_t flags)
{
    portCLEAR_INTERRUPT_MASK_FROM_ISR((UBaseType_t)flags);
}

/* ---- region registration: vPortDefineHeapRegions() over tlsf_config ---- *
 * Each non-zero HeapRegion_t becomes a TLSF heap (ids 1..N). kmalloc(...,
 * MM_FLAG_HEAP_ANY) then spans them via the auto-list. Terminator = size 0
 * (matches heap_5 semantics). */
#define AMEBAD_MM_MAX_HEAPS 4
static uint8_t s_heap_ids[AMEBAD_MM_MAX_HEAPS];
static int     s_heap_count;

void vPortDefineHeapRegions(const HeapRegion_t * const pxHeapRegions)
{
    int i;

    mem_manager_init();          /* registers the TLSF allocator; idempotent */
    s_heap_count = 0;

    for (i = 0; pxHeapRegions[i].xSizeInBytes != 0; i++) {
        uint32_t heap_id = (uint32_t)(i + 1);   /* heap ids are 1.. (0 = invalid) */
        if (heap_id >= CONFIG_MM_HEAP_COUNT) {
            break;
        }
        if (mm_register_heap(heap_id, "rtos", MM_ALLOCATOR_TLSF,
                             (void *)pxHeapRegions[i].pucStartAddress,
                             (size_t)pxHeapRegions[i].xSizeInBytes) != NULL) {
            if (s_heap_count < AMEBAD_MM_MAX_HEAPS) {
                s_heap_ids[s_heap_count] = (uint8_t)heap_id;
            }
            s_heap_count++;
        }
    }
}

/* ---- FreeRTOS heap surface over tlsf_config ---- */
#if defined(configUSE_MALLOC_FAILED_HOOK) && (configUSE_MALLOC_FAILED_HOOK == 1)
extern void vApplicationMallocFailedHook(void);
#define MM_FAIL_HOOK()  vApplicationMallocFailedHook()
#else
#define MM_FAIL_HOOK()  ((void)0)
#endif

/* 8-byte alignment to satisfy FreeRTOS portBYTE_ALIGNMENT on Cortex-M. */
#define MM_RTOS_FLAGS  (MM_FLAG_HEAP_ANY | MM_FLAG_ALIGN_8BYTE)

void *pvPortMalloc(size_t xWantedSize)
{
    void *p = kmalloc(xWantedSize, MM_RTOS_FLAGS);
    if (p == NULL) {
        MM_FAIL_HOOK();
    }
    return p;
}

void vPortFree(void *pv)
{
    if (pv != NULL) {
        kfree(pv);
    }
}

void *pvPortCalloc(size_t xNum, size_t xSize)
{
    void *p = kmalloc(xNum * xSize, MM_RTOS_FLAGS | MM_FLAG_PROP_ZERO);
    if (p == NULL) {
        MM_FAIL_HOOK();
    }
    return p;
}

size_t xPortGetFreeHeapSize(void)
{
    return kfree_size(0);   /* 0 = sum of free bytes across all registered heaps */
}

size_t xPortGetMinimumEverFreeHeapSize(void)
{
    /* tlsf_config min-free tracking is off by default; report current free. */
    return kfree_size(0);
}

void vPortInitialiseBlocks(void)
{
    /* heap is set up by vPortDefineHeapRegions() / os_heap_init(); nothing to do. */
}

void vPortGetHeapStats(HeapStats_t *pxHeapStats)
{
    memset(pxHeapStats, 0, sizeof(*pxHeapStats));
    pxHeapStats->xAvailableHeapSpaceInBytes = kfree_size(0);
}
