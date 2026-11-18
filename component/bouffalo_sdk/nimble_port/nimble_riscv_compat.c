/*
 * nimble_riscv_compat.c — NimBLE's FreeRTOS NPL (porting/npl/freertos/npl_os_freertos.c)
 * calls vPortEnterCritical()/vPortExitCritical(). The ARM ports export those, but the
 * GCC RISC-V port does not: it only offers the portENTER_CRITICAL() macro, which expands
 * to vTaskEnterCritical(). Map them here so the Bouffalo (RISC-V) builds link.
 *
 * On GCC 10 the missing declaration was only a warning and the calls were usually
 * garbage-collected; GCC 14 (bl618dg's Nuclei toolchain) makes it a hard error, which is
 * how this surfaced.
 */
#include "FreeRTOS.h"
#include "task.h"

void vPortEnterCritical( void )
{
    vTaskEnterCritical();
}

void vPortExitCritical( void )
{
    vTaskExitCritical();
}
