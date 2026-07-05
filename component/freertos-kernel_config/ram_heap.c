/*
 * ram_heap.c — the single cross-platform FreeRTOS/TLSF heap registrar.
 *
 * One implementation for all targets (Bouffalo BL602/BL616/BL616CL + AmebaD KM0/KM4),
 * built into the shared `freertos_config` target and called once from each platform's
 * early boot (Bouffalo board_init(); AmebaD app_start). Every platform's linker script
 * exposes the SAME symbol set; this reads only those and registers each as a tlsf_config
 * heap. Replaces the AmebaD os_heap_init()/vPortDefineHeapRegions() indirection and the
 * per-chip Bouffalo board.c copies.
 *
 * Linker-symbol contract (Bouffalo idiom: the *_size / *_start symbol's ADDRESS carries
 * the value, so we take &sym):
 *   __HeapBase / __HeapLimit           main heap span      -> MM_HEAP_OCRAM_0 (always)
 *   _heap_wram_start / _heap_wram_size secondary heap      -> MM_HEAP_WRAM_0  (size 0 = skip)
 *
 * PSRAM (BL616/BL616CL, MM_HEAP_PSRAM_0) is NOT handled here: it needs board-specific
 * hardware bring-up (board_psram_x8_init()), so each such board registers it itself right
 * after calling ram_heap_init() (see the bouffalo board.c board_psram_heap_init()).
 */
#include <stdint.h>
#include <stddef.h>
#include "mm.h"

extern uint32_t __HeapBase;
extern uint32_t __HeapLimit;

/* Weak: a platform with no secondary heap (e.g. AmebaD KM4) leaves these undefined ->
 * they resolve to address 0 -> the guard below skips the heap. Weak also stops the
 * compiler folding `if (&sym)` to always-true (it may assume &strong_symbol != NULL). */
extern uint32_t _heap_wram_start __attribute__((weak));
extern uint32_t _heap_wram_size  __attribute__((weak));

void ram_heap_init(void)
{
    mem_manager_init();

    /* main on-chip RAM heap */
    mm_register_heap(MM_HEAP_OCRAM_0, "OCRAM", MM_ALLOCATOR_TLSF,
                     &__HeapBase, (size_t)&__HeapLimit - (size_t)&__HeapBase);

    /* secondary heap: Bouffalo WiFi-DMA WRAM / AmebaD KM0 ROM-BSS scratch (0 = absent) */
    if ((uintptr_t)&_heap_wram_size) {
        mm_register_heap(MM_HEAP_WRAM_0, "WRAM", MM_ALLOCATOR_TLSF,
                         &_heap_wram_start, (uintptr_t)&_heap_wram_size);
    }
}
