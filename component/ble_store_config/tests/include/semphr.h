#pragma once
#include "FreeRTOS.h"
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void)
{ SemaphoreHandle_t m=malloc(sizeof *m); if(m) assert(!pthread_mutex_init(m,NULL)); return m; }
static inline int xSemaphoreTake(SemaphoreHandle_t m,unsigned wait)
{ (void)wait; return !pthread_mutex_lock(m); }
static inline int xSemaphoreGive(SemaphoreHandle_t m)
{ return !pthread_mutex_unlock(m); }
static inline void vSemaphoreDelete(SemaphoreHandle_t m)
{ assert(!pthread_mutex_destroy(m)); free(m); }
