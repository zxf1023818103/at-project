/*
 * ameba_console_shim.c — adopt the shared console dispatcher (component/console)
 * on the Ameba-newer (ameba-rtos) platform WITHOUT editing the SDK.
 *
 * The SDK's newlib console lives in a submodule
 * (platform/ameba-rtos/.../app/monitor/ram/low_level_io.c) as strong _read/_write.
 * We don't hand-edit the submodule, so instead the project link wraps those two
 * symbols (-Wl,--wrap=_read,_write, set as an INTERFACE link option on at_user in
 * project/ameba/CMakeLists.txt). References to _read/_write then resolve to the
 * __wrap_* below; the SDK's originals stay reachable as __real_*.
 *
 * fd 0/1/2 route to the shared dispatcher (canonical line + echo + stdout/stderr
 * split owned once in component/console); fd >2 fall back to the SDK. The byte
 * backend reuses the SDK's __real__read/__real__write (which drive the LOGUART),
 * so this TU needs no SDK headers and is identical for every ameba-rtos SoC.
 */
#include "console.h"

extern int __real__read(int file, char *ptr, int len);
extern int __real__write(int file, char *ptr, int len);

/* Backend: one byte at a time over the SDK's original LOGUART read/write. */
static void diag_putc(char c)
{
    char b = c;
    __real__write(1, &b, 1);
}

static int diag_getc(void)
{
    char b;
    int n = __real__read(0, &b, 1);
    if (n == 1) {
        return (int)(unsigned char)b;
    }
    if (n == 0) {
        return '\r';   /* SDK _read returns 0 on Enter ('\r'); let the dispatcher map it */
    }
    return -1;
}

static const at_console_backend_t DIAG_BACKEND = {
    diag_putc,   /* putc    -> LOGUART (via __real__write) */
    diag_getc,   /* getc    <- LOGUART (via __real__read)  */
    diag_putc,   /* errputc -> LOGUART                     */
};

/* Strong override of the dispatcher's weak default: zero-config console. */
const at_console_backend_t *at_console_default_backend(void)
{
    return &DIAG_BACKEND;
}

/* --- newlib console syscalls, wrapped --- */
int __wrap__write(int file, char *ptr, int len)
{
    if (file == 1 || file == 2) {
        return at_console_write(file, ptr, len);
    }
    return __real__write(file, ptr, len);
}

int __wrap__read(int file, char *ptr, int len)
{
    if (file == 0) {
        return at_console_read(ptr, len);
    }
    return __real__read(file, ptr, len);
}
