#ifndef AMEBAGREEN2_HEAP_H
#define AMEBAGREEN2_HEAP_H
#include <stddef.h>
#include <stdint.h>
void *pvPortReAlloc(void *ptr, size_t size);
void *pvPortMallocBase(size_t size, uint32_t base);
void *pvPortReAllocBase(void *ptr, size_t size, uint32_t base);
#endif
