#pragma once
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <semphr.h>
#include <timers.h>
/* AP is a single scheduler domain; NP never accesses these host objects. */
typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portNUM_PROCESSORS 1
static inline void pan_enter_critical(void) { taskENTER_CRITICAL(); }
static inline void pan_exit_critical(void) { taskEXIT_CRITICAL(); }
#undef portENTER_CRITICAL
#undef portEXIT_CRITICAL
#define portENTER_CRITICAL(...) pan_enter_critical()
#define portEXIT_CRITICAL(...) pan_exit_critical()
#define portENTER_CRITICAL_ISR(...) pan_enter_critical()
#define portEXIT_CRITICAL_ISR(...) pan_exit_critical()
static inline void pan_yield_from_isr(void) { portYIELD_FROM_ISR(pdTRUE); }
#undef portYIELD_FROM_ISR
#define portYIELD_FROM_ISR(...) pan_yield_from_isr()
#define xPortInIsrContext xPortIsInsideInterrupt
#define xPortGetCoreID() 0
#define tskNO_AFFINITY (-1)
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t bytes,
                                 void *arg, UBaseType_t priority, TaskHandle_t *out, int core);
