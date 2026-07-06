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
 * Malloc-failed hook (TLSF pvPortMalloc). Logs the remaining heap and RETURNS - it does NOT halt.
 * A failed allocation during a network/TLS operation (a bursty handshake, a big transient buffer)
 * must fail *that operation* - mbedTLS / coreMQTT / FreeRTOS+TCP all propagate the NULL as an error
 * and retry - not brick the whole device. (The old fail-fast halt turned every transient OOM under
 * load into a dead chip; see the bl602 TLS-handshake / iperf-RX OOM history.) Logging is rate-limited
 * so a burst of failures can't itself flood the console (or re-enter via a printf allocation).
 */
void vApplicationMallocFailedHook( void )
{
	static uint32_t seen = 0;
	if( ( seen++ & 0x1F ) == 0 )   /* every 32nd failure */
	{
		printf( "\n\r[%s] Malloc failed [free heap size: %d] (op fails + retries; not fatal)\n\r",
		        __FUNCTION__, xPortGetFreeHeapSize() );
	}
}
#endif /* configUSE_MALLOC_FAILED_HOOK */
