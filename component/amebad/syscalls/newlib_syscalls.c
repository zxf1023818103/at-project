/*
 * newlib_syscalls.c — the AmebaD KM4 image's newlib syscall retarget, in one place.
 *
 * The base WiFi image is newlib-stdio-free (it prints via DiagPrintf and allocates
 * from the FreeRTOS heap), but several optional features drag in newlib's reentrant
 * stdio / abort, which need _write / _sbrk / _exit / ...:
 *   - BLE: the vendored BT source uses _rtl_printf + abort  (AMEBAD_ENABLE_HCI/NIMBLE)
 *   - QUIC: ngtcp2's ngtcp2_unreachable() -> abort(), and the full TLS 1.3 / EVP
 *           layer pulls reentrant stdio                      (AMEBAD_ENABLE_QUIC)
 *   - the debug monitor / shell                              (DiagGetChar line input)
 * This file is the single provider, replacing the former three copies
 * (hci/hci_syscalls.c, app_km4/quic_syscalls.c, monitor/low_level_io.c).
 *
 *   _write -> LOGUART console (DiagPutChar), so any newlib-routed print still lands
 *             on COM3; fd is ignored on purpose (BT/TLS prints use various fds).
 *   _read  -> line input from the console (DiagGetChar), for the monitor shell.
 *   _sbrk  -> a small static heap for newlib's stdio buffering (NOT the app heap).
 *   rest   -> inert POSIX stubs newlib expects to exist.
 *
 * GCC/newlib only (this project has no IAR/__ICCARM__ build). Built as an OBJECT
 * library (amebad_syscalls) and force-linked into the KM4 image so these STRONG
 * defs win over libc's default ENOSYS stubs — see CMakeLists.txt.
 */
#include "ameba_soc.h"   /* DiagPutChar / DiagGetChar (ROM console) */

int _write(int fd, char *buf, int len)
{
    int i;
    (void)fd;   /* write regardless of fd so newlib-routed prints reach the console */
    for (i = 0; i < len; i++)
        DiagPutChar((char)buf[i]);
    return len;
}

int _read(int fd, char *buf, int len)
{
    int n = 0;
    (void)fd;
    for (; len > 0; --len) {
        int c = DiagGetChar(_TRUE);      /* blocking, echoes */
        if (c < 0 || c == '\r') {        /* line-oriented: terminate on CR */
            *buf = '\0';
            break;
        }
        *buf++ = (char)c;
        ++n;
    }
    return n;
}

/* newlib's malloc grows the heap through _sbrk. Keep it OFF the FreeRTOS heap
 * (the app's real heap) — a small dedicated arena is enough for stdio buffering. */
static char _nl_heap[4096];
void *_sbrk(int incr)
{
    static char *cur = _nl_heap;
    char *prev = cur;
    if (incr < 0 || cur + incr > _nl_heap + sizeof(_nl_heap))
        return (void *)-1;
    cur += incr;
    return prev;
}

int  _close(int fd)                  { (void)fd; return -1; }
int  _lseek(int fd, int off, int w)  { (void)fd; (void)off; (void)w; return 0; }
int  _fstat(int fd, void *st)        { (void)fd; (void)st; return 0; }
int  _isatty(int fd)                 { (void)fd; return 1; }
int  _kill(int pid, int sig)         { (void)pid; (void)sig; return -1; }
int  _getpid(void)                   { return 1; }
void _exit(int code)                 { (void)code; for (;;) { } }

/* ---------------------------------------------------------------------------
 * TODO (插眼): the rest of newlib's syscall surface. None of the current images
 * reference these — adding strong defs now would just be dead weight (no
 * --gc-sections on the KM4 image2 link). They are listed here so the full
 * retarget is visible in one place; un-#if-0 the ones a future consumer pulls
 * (e.g. a real FS would make _open/_stat/_unlink meaningful; SNTP would make
 * _gettimeofday return real time instead of -1).
 * ------------------------------------------------------------------------- */
#if 0
int    _open(const char *path, int flags, int mode) { (void)path; (void)flags; (void)mode; return -1; }
int    _stat(const char *path, void *st)            { (void)path; (void)st; return -1; }
int    _link(const char *a, const char *b)          { (void)a; (void)b; return -1; }
int    _unlink(const char *path)                    { (void)path; return -1; }
int    _times(void *buf)                            { (void)buf; return -1; }
int    _gettimeofday(void *tv, void *tz)            { (void)tv; (void)tz; return -1; }
int    _fork(void)                                  { return -1; }
int    _execve(const char *p, char *const a[], char *const e[]) { (void)p; (void)a; (void)e; return -1; }
int    _wait(int *status)                           { (void)status; return -1; }
char  *__env[1] = { 0 };
char **environ  = __env;
#endif
