#ifndef __RAM_HEAP_H__
#define __RAM_HEAP_H__

/*
 * ram_heap_init() — the single cross-platform FreeRTOS/TLSF heap registrar.
 *
 * Every platform's linker script exposes the SAME symbol set and this one function
 * (component/freertos-kernel_config/ram_heap.c) registers them as tlsf_config heaps:
 *
 *   __HeapBase / __HeapLimit           -> MM_HEAP_OCRAM_0 "OCRAM" (main, always)
 *   _heap_wram_start / _heap_wram_size -> MM_HEAP_WRAM_0  "WRAM"  (secondary; size 0 = skip)
 *   __psram_heap_base / __psram_limit  -> MM_HEAP_PSRAM_0 "PSRAM" (#ifdef CONFIG_PSRAM)
 *
 * Called once from each platform's early boot: Bouffalo board_init(); AmebaD app_start
 * (KM0 rtl8721dlp_app_start.c, KM4 rtl8721dhp_app_start.c).
 */
void ram_heap_init(void);

#endif /* __RAM_HEAP_H__ */
