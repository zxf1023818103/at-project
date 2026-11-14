#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#ifdef TL3238_ENABLE_CS
#define configCPU_CLOCK_HZ                      96000000UL
#else
#define configCPU_CLOCK_HZ                      48000000UL
#endif
#define configTICK_RATE_HZ                      1000
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_TICKLESS_IDLE                 1
#define configEXPECTED_IDLE_TIME_BEFORE_SLEEP   2
#define configMAX_PRIORITIES                    5
#define configMINIMAL_STACK_SIZE                256
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configSUPPORT_DYNAMIC_ALLOCATION       1
#define configSUPPORT_STATIC_ALLOCATION         0
#ifdef TL3238_ENABLE_CS
/* Reclaimed SMP storage plus unused DRAM; the linker protects the boot stack. */
#ifdef TL3238_ENABLE_COC
/* Four more msys blocks and CoC channel state share the fixed DRAM budget. */
#define configTOTAL_HEAP_SIZE                   (36 * 1024)
#else
#define configTOTAL_HEAP_SIZE                   (38 * 1024)
#endif
#else
#define configTOTAL_HEAP_SIZE                   (32 * 1024)
#endif
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               3
#define configTIMER_QUEUE_LENGTH                8
#define configTIMER_TASK_STACK_DEPTH            256
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    0
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configISR_STACK_SIZE_WORDS              512
#define configCOMMAND_INT_MAX_OUTPUT_SIZE       512
#define configINCLUDE_QUERY_HEADING_COMMAND     0
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskDelayUntil                 1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark      1

#ifndef __ASSEMBLER__
void vAssertCalled(const char *file, unsigned long line);
#define configASSERT(condition) do { if (!(condition)) vAssertCalled(__FILE__, __LINE__); } while (0)
#endif
#endif
