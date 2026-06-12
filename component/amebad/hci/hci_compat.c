/*
 * hci_compat.c — RAM-only fwlib functions the BT source needs that the A-cut ROM
 * symbol table doesn't provide and that KM4's ram_common subset excludes (KM4
 * pulls most UART_* from the HP ROM, so it does NOT compile rtl8721d_uart.c).
 *
 * UART_RxMonitorSatusGet (used by hci_uart.c's RX ISR) is a newer RAM-only fn,
 * not in the ROM table — it's a one-line register read, vendored here verbatim
 * from fwlib/rtl8721d_uart.c so we don't pull the whole TU (which would
 * duplicate the ROM-provided UART_* symbols).
 */
#include <stdarg.h>
#include "ameba_soc.h"

u32 UART_RxMonitorSatusGet(UART_TypeDef *UARTx)
{
    /* reading MON_BAUD_STS also clears the monitor interrupt status */
    return UARTx->MON_BAUD_STS;
}

/* printf() for the vendored BT trace/debug source: route to the ROM
 * _rtl_vsnprintf + DiagPrintf so the -nostdlib image doesn't pull newlib stdio
 * (_write/_sbrk/_exit/...). _rtl_vsnprintf is RAM (rtl8721d_ram_libc.c, in KM4
 * ram_common); DiagPrintf is ROM. Defining a strong printf here keeps libc's
 * printf object from being extracted.
 *
 * GATED on !STD_PRINTF: the ROM _rtl_vsnprintf does NOT support %lx/%u (it prints
 * "format not support!" then dereferences a misparsed vararg -> Hard Fault). That
 * crashes any image that links amebad_hci AND logs with %lx — e.g. littlefs's
 * LFS_ERROR on a corrupted dir. Now that the image carries the newlib retarget
 * (component/syscall_config provides _write/_sbrk/malloc), there's no reason to
 * shadow libc's printf: define STD_PRINTF (CMakeLists) so newlib's printf wins —
 * it handles %lx and routes through our _write to the console. */
#ifndef STD_PRINTF
extern int _rtl_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    _rtl_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return (int)DiagPrintf("%s", buf);
}
#endif
