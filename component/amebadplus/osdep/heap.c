#include "ameba_soc.h"
#include "FreeRTOS.h"
#include "ram_heap.h"
#include "mm.h"
#include "heap.h"
void os_heap_init(void) { ram_heap_init(); }
void *pvPortReAlloc(void *ptr, size_t size) { return krealloc(ptr, size); }
void *pvPortMallocBase(size_t size, uint32_t base) { (void)base; return pvPortMalloc(size); }
void *pvPortReAllocBase(void *ptr, size_t size, uint32_t base) { (void)base; return pvPortReAlloc(ptr, size); }
