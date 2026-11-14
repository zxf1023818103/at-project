#pragma once
#include <pthread.h>
#include <stdlib.h>
#include <assert.h>
typedef pthread_mutex_t *SemaphoreHandle_t;
#define pdTRUE 1
#define portMAX_DELAY 0xffffffffu
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
