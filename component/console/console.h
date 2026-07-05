#ifndef AT_CONSOLE_H
#define AT_CONSOLE_H

/*
 * Cross-platform stdio console (fd 0/1/2) dispatcher.
 *
 * Each platform's newlib retarget (AmebaD component/amebad/syscalls, Bouffalo
 * component/bouffalo_sdk/bflb_libc, ...) routes the three console fds through
 * here; the actual byte I/O is done by a small per-platform backend (DiagPutChar
 * on Ameba, bflb_uart on Bouffalo, an LPUART HAL, ...). The line discipline
 * (canonical read: echo + CR/LF -> '\n') and the stdout/stderr split live here
 * once, shared by every platform.
 */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*putc)(char c);     /* stdout/stdin echo device (required)            */
    int  (*getc)(void);       /* stdin source, blocking; returns <0 if no input */
    void (*errputc)(char c);  /* stderr device; NULL -> use putc                */
} at_console_backend_t;

/* Switch the active backend at runtime (e.g. redirect stdout to another UART).
 * Pass NULL to revert to the platform default (at_console_default_backend). */
void at_console_set_backend(const at_console_backend_t *be);

/* Per-platform zero-config default. A weak no-op lives in console.c; each
 * platform's console code overrides it with a strong definition (e.g. -> LOGUART
 * / bflb console) so stdout works before any explicit registration. */
const at_console_backend_t *at_console_default_backend(void);

/* newlib _read/_write helpers for the console fds. */
int at_console_read(char *buf, int len);                /* fd 0: canonical line  */
int at_console_write(int fd, const char *buf, int len); /* fd 0/1->putc, 2->err  */

/* Raw byte I/O straight to the active backend, bypassing the line discipline: no
 * timestamp prefix (at_console_write_raw) and no echo / CR-LF canonicalisation
 * (at_console_getc). The interactive console (component/monitor_config) uses these
 * for its prompt / echo / command output (which must NOT be timestamped) and its
 * own line editor. at_console_getc returns the byte (0..255) or <0 when idle. */
void at_console_write_raw(const char *buf, int len);
int  at_console_getc(void);

/* Per-line log timestamp prefix. at_console_write() calls this at the start of
 * every output line and emits the returned bytes before the line's content, so
 * all printf-routed logs are timestamped with no call-site changes. Writes the
 * prefix (e.g. "[12:34:56.789] ") into buf and returns its length (0 = none).
 * A weak no-op lives in console.c; the platform's time module overrides it with
 * a strong definition that has the clock (e.g. syscall_config/time_base.c). */
int at_console_timestamp(char *buf, int cap);

#ifdef __cplusplus
}
#endif

#endif /* AT_CONSOLE_H */
