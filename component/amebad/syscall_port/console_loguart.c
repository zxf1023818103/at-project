/*
 * console_loguart.c — the default AmebaD console backend: the LOGUART ROM console
 * (DiagPutChar / DiagGetChar).
 *
 * Provides the strong at_console_default_backend() that the cross-platform console
 * dispatcher (component/console) falls back to, so stdout/printf/std::cerr work on
 * the LOGUART with no explicit setup — exactly the behaviour the old inline
 * fd 0/1/2 path in newlib_syscalls.c had. console_lpuart.c can override this at
 * runtime (at_console_set_backend) to move stdin/stdout to the LPUART.
 *
 * Always linked into amebad_syscalls so every image has a working console.
 */
#include "ameba_soc.h"   /* DiagPutChar / DiagGetChar (ROM console) */
#include "console.h"

static void lg_putc(char c) { DiagPutChar(c); }
static int  lg_getc(void)   { return DiagGetChar(_TRUE); }   /* blocks for a char */

static const at_console_backend_t LOGUART_BACKEND = {
    lg_putc,   /* putc    -> LOGUART */
    lg_getc,   /* getc    <- LOGUART */
    lg_putc,   /* errputc -> LOGUART */
};

const at_console_backend_t *at_console_default_backend(void)
{
    return &LOGUART_BACKEND;
}
