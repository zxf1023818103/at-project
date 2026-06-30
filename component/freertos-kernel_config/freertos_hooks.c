/*
 * AmebaD FreeRTOS application hooks.
 *
 * The SDK's FreeRTOS v10.2.0 ports (RTL8721D_LP/port.c, RTL8721D_HP/non_secure/
 * port.c) bolted the required application hooks onto port.c. The upstream
 * FreeRTOS-Kernel V10.6.2 ARM_CM23/CM33 ports we now use are stock and leave the
 * hooks to the application, so they are provided here (the SDK FreeRTOSConfig.h
 * keeps configUSE_IDLE_HOOK 1 and configCHECK_FOR_STACK_OVERFLOW 2, both of
 * which the kernel references). Chip-agnostic glue, compiled per-ABI for KM0 and
 * KM4 alongside the other freertos_glue sources.
 */
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>   /* printf — on AmebaD KM0 (no libc) it's aliased to the ROM
                      * DiagPrintfD in app_km0/rlx8721d_img2_romalias.ld */

/* Each hook is gated on the config option that makes the kernel reference it,
 * so this one file is correct for both cores (KM0 inc_lp and KM4 inc_hp differ —
 * e.g. inc_lp has configUSE_MALLOC_FAILED_HOOK 0, inc_hp has it 1). */

#if ( configUSE_IDLE_HOOK == 1 )
/*
 * Idle hook. The SDK LP port called app_taskidle_handler() here for low-power
 * management; the HP port left it empty. The minimal driver-only image does not
 * pull in the SDK power-management app, so this is intentionally a no-op. Wire
 * up low-power idle handling here when PMU support is brought up.
 */
void vApplicationIdleHook( void )
{
}
#endif /* configUSE_IDLE_HOOK */

#if ( configCHECK_FOR_STACK_OVERFLOW > 0 )
/*
 * Stack overflow hook. Mirrors the SDK port.c behaviour: report the offending
 * task over the log UART and halt.
 */
void vApplicationStackOverflowHook( TaskHandle_t xTask, char *pcTaskName )
{
	( void ) xTask;

	printf( "\n\r[%s] STACK OVERFLOW - TaskName(%s)\n\r", __FUNCTION__, pcTaskName );
	for( ;; )
	{
	}
}
#endif /* configCHECK_FOR_STACK_OVERFLOW */

#if ( configUSE_MALLOC_FAILED_HOOK == 1 )
/*
 * Malloc-failed hook (heap_5 pvPortMalloc). Reports the remaining heap and
 * halts. Kept dependency-light (no optional INCLUDE_* task APIs) so it compiles
 * regardless of which task-introspection helpers the config includes.
 */
void vApplicationMallocFailedHook( void )
{
	printf( "\n\r[%s] Malloc failed [free heap size: %d]\n\r", __FUNCTION__, xPortGetFreeHeapSize() );
	taskDISABLE_INTERRUPTS();
	for( ;; )
	{
	}
}
#endif /* configUSE_MALLOC_FAILED_HOOK */
