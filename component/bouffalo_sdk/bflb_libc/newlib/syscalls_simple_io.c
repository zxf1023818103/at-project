#include <unistd.h>
#include <stdio.h>
#include <reent.h>

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

ssize_t _console_write_r(struct _reent *reent, int fd, const void *ptr, size_t size)
{
    (void)reent;
    if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
        return at_console_write(fd, (const char *)ptr, (int)size);
    }
    return -1;
}

#ifndef CONFIG_CONSOLE_WO
ssize_t _console_read_r(struct _reent *reent, int fd, void *ptr, size_t size)
{
    (void)reent;
    if (fd == STDIN_FILENO) {
        return at_console_read((char *)ptr, (int)size);
    }
    return -1;
}
#endif

#ifndef CONFIG_CONSOLE_WO
ssize_t _read_r(struct _reent *r, int fd, void *dst, size_t size)
__attribute__((weak, alias("_console_read_r")));
#endif
ssize_t _write_r(struct _reent *r, int fd, const void *data, size_t size)
__attribute__((weak, alias("_console_write_r")));
