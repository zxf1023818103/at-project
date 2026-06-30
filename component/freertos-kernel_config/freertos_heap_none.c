/*
 * freertos_heap_none.c — empty heap source for the upstream FreeRTOS-Kernel.
 *
 * component/amebad/freertos sets FREERTOS_HEAP to this file so the kernel does NOT
 * compile any portable/MemMang/heap_N.c. The FreeRTOS heap surface
 * (pvPortMalloc/vPortFree/vPortDefineHeapRegions/...) is supplied instead by
 * freertos_heap_tlsf.c (in amebad_freertos_glue, which has the tlsf_config + SDK
 * includes), backed by the shared tlsf_config allocator.
 */
typedef int freertos_heap_none_translation_unit_not_empty;
