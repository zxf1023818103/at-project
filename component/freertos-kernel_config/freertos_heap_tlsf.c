/*
 * freertos_heap_tlsf.c — the cross-platform FreeRTOS heap surface backed by the shared
 * tlsf_config allocator (component/tlsf_config, TLSF), replacing the kernel's heap_5.c.
 *
 * The kernel's heap source is disabled (FREERTOS_HEAP points at the empty
 * freertos_heap_none.c), so this file provides the FreeRTOS heap surface
 * (pvPortMalloc/vPortFree/...) over kmalloc/kfree. The heap REGIONS themselves are
 * registered by ram_heap_init() (ram_heap.c) directly from each platform's linker symbols.
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

/* Heap-region registration lives in the shared ram_heap_init()
 * (component/freertos-kernel_config/ram_heap.c): it calls mem_manager_init() + mm_register_heap()
 * directly from the platform's linker symbols. This file only provides the pvPortMalloc surface
 * below over the resulting tlsf_config heaps. (The old vPortDefineHeapRegions() shim is gone.) */

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
#if defined(CONFIG_MM_ENABLE_MIN_FREE_TRACKING) && CONFIG_MM_ENABLE_MIN_FREE_TRACKING
    /* Real low-water watermark: mm.c updates min_free_size after every malloc when
     * CONFIG_MM_ENABLE_MIN_FREE_TRACKING is on (O(n)-per-malloc; see mm_config.h). */
    return kmin_free_size();
#else
    /* tracking off (default): no per-malloc overhead; report current free. */
    return kfree_size(0);
#endif
}

/* Re-baseline the min-ever-free watermark to the current free (the `heapreset` console command).
 * Only meaningful when CONFIG_MM_ENABLE_MIN_FREE_TRACKING is on; otherwise a no-op. Lets you
 * isolate a phase's low-water (e.g. reset right before a TLS handshake / OTA download, then read
 * `heap`). Not a standard FreeRTOS API. */
void vPortResetMinimumEverFreeHeapSize(void)
{
#if defined(CONFIG_MM_ENABLE_MIN_FREE_TRACKING) && CONFIG_MM_ENABLE_MIN_FREE_TRACKING
    (void)kmin_free_size_reset();
#endif
}

void vPortInitialiseBlocks(void)
{
    /* heap is set up by ram_heap_init(); nothing to do. */
}

void vPortGetHeapStats(HeapStats_t *pxHeapStats)
{
    memset(pxHeapStats, 0, sizeof(*pxHeapStats));
    pxHeapStats->xAvailableHeapSpaceInBytes = kfree_size(0);
}
