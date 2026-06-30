/*
 * freertos_static_mem.c — static memory for the Idle + Timer tasks.
 *
 * Required once configSUPPORT_STATIC_ALLOCATION == 1 (now enabled for all platforms in
 * FreeRTOSConfig.h): this FreeRTOS (V10.6.2) has no configKERNEL_PROVIDED_STATIC_MEMORY default,
 * so the kernel calls vApplicationGetIdle/TimerTaskMemory to get the idle/timer task's static
 * buffers. Built per-core via freertos_port, so configMINIMAL_STACK_SIZE / configTIMER_TASK_STACK_DEPTH
 * resolve to the KM0 vs KM4 values. Weak so a platform image may override if it needs custom sizing.
 */
#include "FreeRTOS.h"
#include "task.h"

__attribute__( ( weak ) )
void vApplicationGetIdleTaskMemory( StaticTask_t ** ppxIdleTaskTCBBuffer,
                                    StackType_t ** ppxIdleTaskStackBuffer,
                                    uint32_t * pulIdleTaskStackSize )
{
    static StaticTask_t xIdleTCB;
    static StackType_t  uxIdleStack[ configMINIMAL_STACK_SIZE ];

    *ppxIdleTaskTCBBuffer   = &xIdleTCB;
    *ppxIdleTaskStackBuffer = uxIdleStack;
    *pulIdleTaskStackSize   = configMINIMAL_STACK_SIZE;
}

#if ( configUSE_TIMERS == 1 )
__attribute__( ( weak ) )
void vApplicationGetTimerTaskMemory( StaticTask_t ** ppxTimerTaskTCBBuffer,
                                     StackType_t ** ppxTimerTaskStackBuffer,
                                     uint32_t * pulTimerTaskStackSize )
{
    static StaticTask_t xTimerTCB;
    static StackType_t  uxTimerStack[ configTIMER_TASK_STACK_DEPTH ];

    *ppxTimerTaskTCBBuffer   = &xTimerTCB;
    *ppxTimerTaskStackBuffer = uxTimerStack;
    *pulTimerTaskStackSize   = configTIMER_TASK_STACK_DEPTH;
}
#endif
