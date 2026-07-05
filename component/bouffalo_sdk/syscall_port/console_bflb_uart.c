/*
 * component/bouffalo_sdk/syscall_port — Bouffalo's console backend for the
 * cross-platform newlib retarget. The retarget core (POSIX fd VFS + malloc +
 * env + time + sockets) is the shared component/syscall_config; what stays here
 * is the platform-specific byte I/O: the bflb_uart console, registered as the
 * strong at_console_default_backend for the shared dispatcher (component/console).
 *
 * Extracted from the old bflb_libc/newlib/syscalls_simple_io.c (its _read_r/
 * _write_r retarget is dropped — syscall_config owns fd 0/1/2 now).
 */
#include <stdarg.h>
#include <stdio.h>     /* vsnprintf (malloc-free for the %x DiagPrintf below) */

#ifdef CONFIG_CONSOLE_WO
#include "bflb_wo.h"
#else
#include "bflb_uart.h"
#endif

#include "console.h"   /* cross-platform fd 0/1/2 dispatcher (component/console) */

struct bflb_device_s *console = NULL;

#ifdef CONFIG_CONSOLE_WO
void bflb_wo_set_console(struct bflb_device_s *dev)
#else
void bflb_uart_set_console(struct bflb_device_s *dev)
#endif
{
    console = dev;
}

#ifdef CONFIG_CONSOLE_WO
    #define CONSOLE_PUTCHAR(console, x) bflb_wo_uart_putchar(console, x)
#else
    #define CONSOLE_PUTCHAR(console, x) bflb_uart_putchar(console, x)
#endif

/* Bouffalo console backend for the shared dispatcher: bytes go to the global
 * `console` UART device. putc keeps the SDK's '\n' -> "\r\n" expansion so log
 * output formatting is unchanged. */
static void bflb_console_putc(char c)
{
    if (c == '\n') {
        CONSOLE_PUTCHAR(console, '\r');
    }
    CONSOLE_PUTCHAR(console, c);
}

#ifndef CONFIG_CONSOLE_WO
static int bflb_console_getc(void)
{
    return bflb_uart_getchar(console);   /* blocks; <0 if no input */
}
#endif

static const at_console_backend_t BFLB_CONSOLE = {
    bflb_console_putc,                   /* putc    */
#ifndef CONFIG_CONSOLE_WO
    bflb_console_getc,                   /* getc    */
#else
    0,                                   /* write-only console (WO) */
#endif
    bflb_console_putc,                   /* errputc -> same console */
};

/* Strong override of the dispatcher's weak default: zero-config console. */
const at_console_backend_t *at_console_default_backend(void)
{
    return &BFLB_CONSOLE;
}

/* AmebaD names its ROM console print DiagPrintf; component/syscall_config's malloc
 * uses it for the OOM/overflow diagnostic — deliberately NOT printf, since that path
 * runs inside malloc() and newlib printf can itself allocate. Provide the Bouffalo
 * DiagPrintf here: vsnprintf into a stack buffer (malloc-free for the %x it uses) +
 * a direct uart write, so it allocates nothing. AmebaD gets DiagPrintf from ROM. */
void DiagPrintf(const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) {
        return;
    }
    if (n > (int)sizeof(buf) - 1) {
        n = (int)sizeof(buf) - 1;
    }
    at_console_write(2, buf, n);   /* fd 2 = stderr; direct to the console backend */
}
