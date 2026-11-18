#pragma once
#include <stddef.h>
#ifdef CONFIG_PSRAM
#define NAN_DATA_HEAP MM_HEAP_PSRAM_0
#define NAN_DATA_HEAP_FLAGS MM_FLAG_HEAP_PSRAM_0
#define NAN_DATA_HEAP_NAME "PSRAM"
#else
#define NAN_DATA_HEAP MM_HEAP_WRAM_0
#define NAN_DATA_HEAP_FLAGS MM_FLAG_HEAP_WRAM_0
#define NAN_DATA_HEAP_NAME "WRAM"
#endif
void *bl618dg_nan_malloc(size_t size);
void bl618dg_nan_heap_status(void);
void bl618dg_nan_heap_selftest(void);
