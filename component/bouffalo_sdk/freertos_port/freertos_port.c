/*
 * freertos_port.c — Bouffalo-only FreeRTOS port glue.
 *
 * The cross-platform FreeRTOS extensions live in component/freertos-kernel_config's
 * freertos_config target:
 *   - the idle / stack-overflow / malloc-failed hooks      -> freertos_hooks.c
 *   - the idle + timer task static-allocation buffers       -> freertos_static_mem.c
 * so only the Bouffalo-specific bits remain here: the backtrace-aware assert hook and the
 * newlib __errno() that maps to FreeRTOS's per-task errno.
 */
#include <stdio.h>
#include <stdlib.h>

#include <FreeRTOS.h>
#include "semphr.h"
#if defined(CONFIG_BACKTRACE)
#include "backtrace.h"
#endif

__attribute__((weak)) void vAssertCalled(void)
{
#if defined(CONFIG_BACKTRACE)
    backtrace_now();
#endif
    printf("vAssertCalled\r\n");
    abort();
}

int *__errno()
{
    //#if(configUSE_POSIX_ERRNO == 1)
      {
      extern int FreeRTOS_errno;
      return &FreeRTOS_errno;
      }
    //#endif
    //return &_REENT->_errno;
}
