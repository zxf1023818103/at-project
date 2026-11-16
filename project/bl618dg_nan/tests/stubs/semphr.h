#pragma once
typedef void *SemaphoreHandle_t;
static inline void *xSemaphoreCreateMutex(void) { return (void *)1; }
static inline int xSemaphoreTake(void *p, unsigned wait) { (void)p; (void)wait; return 1; }
static inline int xSemaphoreGive(void *p) { (void)p; return 1; }
