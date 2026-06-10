#ifndef AT_FREERTOS_CONFIG_COMMON_H
#define AT_FREERTOS_CONFIG_COMMON_H
/*
 * Cross-platform FreeRTOS config — the settings that are IDENTICAL across all the
 * project's FreeRTOS builds: AmebaD KM0 (Cortex-M23) + KM4 (Cortex-M33) and
 * Bouffalo (RISC-V). Each platform's own FreeRTOSConfig.h defines its hardware /
 * port specifics (clock, heap size, priority count, Cortex-M interrupt-priority
 * bits vs RISC-V MTIME, FPU/MPU/TrustZone, hooks that differ, configASSERT, ...)
 * and then `#include "freertos_config_common.h"` for this shared feature set.
 *
 * Every define is #ifndef-guarded, so a platform that needs a different value can
 * still set it BEFORE including this header and win. The values here are exactly
 * the ones all three configs already agreed on — so this relocation is value-
 * preserving, not a behaviour change.
 */

/* Scheduler behaviour. */
#ifndef configUSE_PREEMPTION
#define configUSE_PREEMPTION                    1
#endif
#ifndef configUSE_TIME_SLICING
#define configUSE_TIME_SLICING                  1
#endif
#ifndef configIDLE_SHOULD_YIELD
#define configIDLE_SHOULD_YIELD                 0
#endif
#ifndef configUSE_16_BIT_TICKS
#define configUSE_16_BIT_TICKS                  0   /* Only for 8/16-bit hardware. */
#endif
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ                      ( ( TickType_t ) 1000 )
#endif

/* Synchronisation primitives. */
#ifndef configUSE_MUTEXES
#define configUSE_MUTEXES                       1
#endif
#ifndef configUSE_RECURSIVE_MUTEXES
#define configUSE_RECURSIVE_MUTEXES             1
#endif
#ifndef configUSE_COUNTING_SEMAPHORES
#define configUSE_COUNTING_SEMAPHORES           1
#endif

/* Software timers. */
#ifndef configUSE_TIMERS
#define configUSE_TIMERS                        1
#endif
#ifndef configMAX_CO_ROUTINE_PRIORITIES
#define configMAX_CO_ROUTINE_PRIORITIES         ( 2 )
#endif

/* Diagnostics / introspection. */
#ifndef configUSE_TRACE_FACILITY
#define configUSE_TRACE_FACILITY                1
#endif
#ifndef configUSE_STATS_FORMATTING_FUNCTIONS
#define configUSE_STATS_FORMATTING_FUNCTIONS    1
#endif
#ifndef configCHECK_FOR_STACK_OVERFLOW
#define configCHECK_FOR_STACK_OVERFLOW          2
#endif
#ifndef configUSE_TICK_HOOK
#define configUSE_TICK_HOOK                     0
#endif
#ifndef configGENERATE_RUN_TIME_STATS
#define configGENERATE_RUN_TIME_STATS           0
#endif

/* INCLUDE_* — the API set all three platforms enable. (Platform-specific INCLUDEs,
 * e.g. xTaskAbortDelay / pcTaskGetTaskName / uxTaskGetStackSize, stay per-platform.) */
#ifndef INCLUDE_vTaskPrioritySet
#define INCLUDE_vTaskPrioritySet                1
#endif
#ifndef INCLUDE_uxTaskPriorityGet
#define INCLUDE_uxTaskPriorityGet               1
#endif
#ifndef INCLUDE_vTaskDelete
#define INCLUDE_vTaskDelete                     1
#endif
#ifndef INCLUDE_vTaskSuspend
#define INCLUDE_vTaskSuspend                    1
#endif
#ifndef INCLUDE_vTaskDelayUntil
#define INCLUDE_vTaskDelayUntil                 1
#endif
#ifndef INCLUDE_vTaskDelay
#define INCLUDE_vTaskDelay                      1
#endif
#ifndef INCLUDE_eTaskGetState
#define INCLUDE_eTaskGetState                   1
#endif
#ifndef INCLUDE_xTimerPendFunctionCall
#define INCLUDE_xTimerPendFunctionCall          1
#endif

#endif /* AT_FREERTOS_CONFIG_COMMON_H */
