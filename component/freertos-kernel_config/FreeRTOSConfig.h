/*
 * FreeRTOSConfig.h — single cross-platform FreeRTOS config for the whole project.
 *
 * One header, platform-selected by preprocessor macros (no per-platform copies):
 *   - AmebaD (RTL8721D):  CONFIG_PLATFORM_8721D
 *       - KM4 (Cortex-M33, HP):  __ARM_ARCH_8M_MAIN__   (armv8-m.main)
 *       - KM0 (Cortex-M23, LP):  __ARM_ARCH_8M_BASE__   (armv8-m.base, the #else)
 *   - Bouffalo (RISC-V):  BL602 / BL616 / BL616CL / ...
 *
 * Values are exactly those the former per-platform FreeRTOSConfig.h carried (this
 * is a consolidation, not a behaviour change). The AmebaD driver-only tickless-idle
 * disable that used to live in the freertos/config #include_next override is folded
 * into the AmebaD branch below.
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* ---- AmebaD preamble (SystemCoreClock + Kconfig autoconf used in its branch) ---- */
#if defined(CONFIG_PLATFORM_8721D)
#if defined(__ICCARM__) || defined(__CC_ARM) || defined(__GNUC__)
#include <stdint.h>
extern uint32_t SystemCoreClock;
#endif
#ifndef __IASMARM__
#define __IASMARM__ 0 /* IAR */
#endif
#include "platform_autoconf.h"
#endif

/* ====================================================================== *
 *  Common to every platform (AmebaD KM0/KM4 + Bouffalo).                  *
 * ====================================================================== */
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configIDLE_SHOULD_YIELD                 0
#define configUSE_16_BIT_TICKS                  0   /* Only for 8/16-bit hardware. */
#define configTICK_RATE_HZ                      ( ( TickType_t ) 1000 )
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configSUPPORT_STATIC_ALLOCATION         1   /* always on, all platforms: static FreeRTOS objects
                                                     * (e.g. rxi_log_config's lock mutex). Requires the app to
                                                     * supply vApplicationGetIdle/TimerTaskMemory — AmebaD's
                                                     * are in component/amebad/freertos_port. */
#define configUSE_TIMERS                        1
#define configMAX_CO_ROUTINE_PRIORITIES         ( 2 )
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    1
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_TICK_HOOK                     0
#define configGENERATE_RUN_TIME_STATS           0
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xTimerPendFunctionCall          1

/* ====================================================================== *
 *  AmebaD (RTL8721D), KM0 + KM4.                                          *
 * ====================================================================== */
#if defined(CONFIG_PLATFORM_8721D)

/* ---- both cores ---- */
#define configENABLE_MPU                                0
#if defined(CONFIG_TRUSTZONE_EN) && (CONFIG_TRUSTZONE_EN == 1)
#define configENABLE_TRUSTZONE                          1
#else
#define configENABLE_TRUSTZONE                          0
#endif
#define configRUN_FREERTOS_SECURE_ONLY                  0
#define configUSE_PORT_OPTIMISED_TASK_SELECTION         0
#define configMAX_PRIORITIES                            ( 11 )
#define configCPU_CLOCK_HZ                              SystemCoreClock
#define configMINIMAL_STACK_SIZE                        ( ( unsigned short ) 512 )
#define configMINIMAL_SECURE_STACK_SIZE                 ( 1024 )
#define configMAX_TASK_NAME_LEN                         ( 10 )
#define configUSE_NEWLIB_REENTRANT                      0
#define configUSE_CO_ROUTINES                           1
#define configUSE_QUEUE_SETS                            1
#define configUSE_TASK_NOTIFICATIONS                    1
#define configQUEUE_REGISTRY_SIZE                       0
#define configRECORD_STACK_HIGH_ADDRESS                 1
#define configTIMER_TASK_PRIORITY                       1
#define configTIMER_TASK_STACK_DEPTH                    ( 512 )
#define configCOMMAND_INT_MAX_OUTPUT_SIZE               2048
#define PRIORITIE_OFFSET                                ( 4 )   /* Realtek wlan task prio offset */
/* INCLUDE_ deltas shared by both cores */
#define INCLUDE_vTaskCleanUpResources                   0
#define INCLUDE_pcTaskGetTaskName                       1
#define INCLUDE_uxTaskGetStackHighWaterMark             0
#define INCLUDE_xTaskGetIdleTaskHandle                  0
#define INCLUDE_xTaskResumeFromISR                      0
#define INCLUDE_xTaskGetCurrentTaskHandle               1
#define INCLUDE_xTaskGetSchedulerState                  0

#if defined(__ARM_ARCH_8M_MAIN__)
/* ----------------- KM4 (Cortex-M33, HP, non-secure) ----------------- */
#define configENABLE_FPU                                1
#ifdef CONFIG_WIFI_EN
#define configTOTAL_HEAP_SIZE                           ( ( size_t ) ( 250 * 1024 ) )
#if (defined CONFIG_HIGH_TP_TEST)
#undef configTOTAL_HEAP_SIZE
#define configTOTAL_HEAP_SIZE                           ( ( size_t ) ( 100 * 1024 ) )
#endif
#else
#define configTOTAL_HEAP_SIZE                           ( ( size_t ) ( 40 * 1024 ) )
#endif
#define CONFIG_DYNAMIC_HEAP_SIZE                        1
#define configUSE_PSRAM_FOR_HEAP_REGION                 0
#define configAUDIO_USE_SRAM_FOR_HEAP_REGION            0
#define secureconfigTOTAL_SRAM_HEAP_SIZE                ( ( ( size_t ) ( 6 * 1024 ) ) )
#define secureconfigTOTAL_PSRAM_HEAP_SIZE               ( ( ( size_t ) ( 128 * 1024 ) ) )
#define configUSE_APPLICATION_TASK_TAG                  1
#define configUSE_IDLE_HOOK                             0
#define configUSE_MALLOC_FAILED_HOOK                    1
#define configTIMER_QUEUE_LENGTH                        ( 10 + 64 )
#define INCLUDE_uxTaskGetStackSize                      1
#define INCLUDE_uxTaskGetFreeStackSize                  1
#define INCLUDE_xSemaphoreGetMutexHolder                1
#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS                                 __NVIC_PRIO_BITS
#else
#define configPRIO_BITS                                 3
#endif
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         7
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5

#else
/* ----------------- KM0 (Cortex-M23, LP) ----------------- */
/* (Software timers are ON, via the common section. They used to crash KM0's scheduler start, but
 * the real cause was heap exhaustion — the 20 KB ucHeap couldn't fit the timer task+queue, so
 * xTimerCreateTimerTask's malloc returned NULL, vTaskStartScheduler returned, and KM0 unwound
 * all the way out to the boot ROM (0x49a2 dead-loop). See doc/amebad_km0_scheduler_and_runtime_flash.md.
 * Now configSUPPORT_STATIC_ALLOCATION is on (common): the idle + timer TASKS are static (.bss, via
 * freertos_port/freertos_static_mem.c), off-heap — so the heap no longer needs the ~1.7 KB those
 * tasks took, and it's trimmed to 24 KB to make BD_RAM room for the new static .bss.) */
#define configENABLE_FPU                                0
#define configTOTAL_HEAP_SIZE                           ( ( size_t ) ( 24 * 1024 ) ) /* idle/timer tasks now static (.bss), off-heap; BD_RAM ceiling ~31K from ucHeap@~0x87460. */
#define configUSE_APPLICATION_TASK_TAG                  0
#define configUSE_IDLE_HOOK                             1
#define configUSE_MALLOC_FAILED_HOOK                    0
#define configTIMER_QUEUE_LENGTH                        10
#define INCLUDE_uxTaskGetStackSize                      0
#define INCLUDE_uxTaskGetFreeStackSize                  0
#define INCLUDE_xSemaphoreGetMutexHolder                0
#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS                                 __NVIC_PRIO_BITS
#else
#define configPRIO_BITS                                 2
#endif
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         3
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    2
#endif /* KM4 vs KM0 */

#define configKERNEL_INTERRUPT_PRIORITY                 ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY            ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

/* Port interrupt handlers -> CMSIS vector names. */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#if (__IASMARM__ != 1)
#if configGENERATE_RUN_TIME_STATS
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()
#define portGET_RUN_TIME_COUNTER_VALUE()                RTIM_GetCount(0x48002000/*TIMM00*/)
#undef  configUSE_TRACE_FACILITY
#define configUSE_TRACE_FACILITY                        1
#define portCONFIGURE_STATS_PEROID_VALUE                1000
#else
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()
#define portGET_RUN_TIME_COUNTER_VALUE()                xTickCount
#endif

/* Driver-only firmware: tickless idle OFF. The upstream ARM_CM23/CM33 ports gate
 * ALL tickless code on configUSE_TICKLESS_IDLE == 1, so 0 drops the external SDK
 * PMU sleep refs entirely. (This is what the old freertos/config override did.) */
#define configUSE_TICKLESS_IDLE                         0
#define configEXPECTED_IDLE_TIME_BEFORE_SLEEP           2
#define configPRE_SLEEP_PROCESSING( x )                 ( freertos_pre_sleep_processing((unsigned int *)&x) )
#define configPOST_SLEEP_PROCESSING( x )                ( freertos_post_sleep_processing((unsigned int *)&x) )
#define portSUPPRESS_TICKS_AND_SLEEP( xExpectedIdleTime ) vPortSuppressTicksAndSleep( xExpectedIdleTime )
extern void freertos_pre_sleep_processing(unsigned int *expected_idle_time);
extern void freertos_post_sleep_processing(unsigned int *expected_idle_time);
extern int  freertos_ready_to_sleep(void);
#define traceLOW_POWER_IDLE_BEGIN()
#define traceLOW_POWER_IDLE_END()
#define configUSE_WAKELOCK_PMU                          1
#endif /* __IASMARM__ */

#define configASSERT( x )   /* no-op (matches SDK driver-only config) */

/* ====================================================================== *
 *  Bouffalo (RISC-V): BL602 / BL616 / BL616CL / ...                       *
 *  Gated on __riscv (the RISC-V toolchain built-in), NOT on BLxxx: the BL*
 *  chip macro comes from bflb autoconf.h and is NOT on the FreeRTOS kernel's
 *  compile line, whereas __riscv always is. The BL* test is used only inside
 *  the #ifndef-guarded MTIME block below (the kernel skips it via -D).        *
 * ====================================================================== */
#elif defined(__riscv)

#include <stdio.h>

/* The build also passes -DconfigMTIME(CMP)_BASE_ADDRESS via bflb_freertos_port;
 * guard with #ifndef so this header doesn't redefine. */
#ifndef configMTIME_BASE_ADDRESS
#if defined(BL602) || defined(BL702) || defined(BL702L)
#define configMTIME_BASE_ADDRESS    (0x02000000UL + 0xBFF8UL)
#define configMTIMECMP_BASE_ADDRESS (0x02000000UL + 0x4000UL)
#else
#define configMTIME_BASE_ADDRESS    (0xE0000000UL + 0xBFF8UL)
#define configMTIMECMP_BASE_ADDRESS (0xE0000000UL + 0x4000UL)
#endif
#endif

/* configSUPPORT_STATIC_ALLOCATION moved to the common section (always on, all platforms). */
#define configUSE_IDLE_HOOK                      0
#define configCPU_CLOCK_HZ                       ((uint32_t)(1 * 1000 * 1000))
#define configMAX_PRIORITIES                     (32)
#define configMINIMAL_STACK_SIZE                 ((unsigned short)128)
#define configTOTAL_HEAP_SIZE                    ((size_t)60 * 1024)
#define configMAX_TASK_NAME_LEN                  (16)
#define configQUEUE_REGISTRY_SIZE                8
#define configUSE_MALLOC_FAILED_HOOK             1
#define configUSE_APPLICATION_TASK_TAG           1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  1
#define configUSE_TICKLESS_IDLE                  0
#define configUSE_POSIX_ERRNO                    1
#define configUSE_CO_ROUTINES                    0
#define configTIMER_TASK_PRIORITY                (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                 4
#define configTIMER_TASK_STACK_DEPTH             (1024)
#ifndef uartPRIMARY_PRIORITY
#define uartPRIMARY_PRIORITY                     (configMAX_PRIORITIES - 3)
#endif
#define INCLUDE_vTaskCleanUpResources            1
#define INCLUDE_xTaskAbortDelay                  1
#define INCLUDE_xTaskGetHandle                   1
#define INCLUDE_xSemaphoreGetMutexHolder         1

void vApplicationMallocFailedHook(void);
void vAssertCalled(void);
#define configASSERT(x)                        \
    if ((x) == 0) {                            \
        printf("file [%s]\r\n", __FILE__);     \
        printf("func [%s]\r\n", __FUNCTION__); \
        printf("line [%d]\r\n", __LINE__);     \
        printf("%s\r\n", (const char *)(#x));  \
        vAssertCalled();                       \
    }
#if (configUSE_TICKLESS_IDLE != 0)
void vApplicationSleep(uint32_t xExpectedIdleTime);
#define portSUPPRESS_TICKS_AND_SLEEP(xExpectedIdleTime) vApplicationSleep(xExpectedIdleTime)
#endif

#else
#error "FreeRTOSConfig.h: unknown platform (expected CONFIG_PLATFORM_8721D or a BLxxx macro)"
#endif

#endif /* FREERTOS_CONFIG_H */
