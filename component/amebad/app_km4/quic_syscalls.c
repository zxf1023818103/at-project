/*
 * quic_syscalls.c — minimal newlib syscall retarget for the QUIC test image.
 *
 * ngtcp2's ngtcp2_unreachable() -> abort() pulls newlib's _exit/_kill/_getpid,
 * and the full TLS 1.3 / EVP layer drags in newlib reentrant stdio (_write/
 * _sbrk/...). The base WiFi image is newlib-stdio-free (it prints via DiagPrintf
 * and allocates via the FreeRTOS heap), so nothing else provides these.
 *   _write -> LOGUART  (newlib-routed prints still reach the console)
 *   _sbrk  -> a small static heap for newlib's stdio buffering
 *   rest   -> inert stubs
 * Only linked into the QUIC image (AMEBAD_ENABLE_QUIC). Mirrors hci_syscalls.c.
 */

extern void LOGUART_PutChar(unsigned char c);   /* ROM (== DiagPutChar) */

int _write(int fd, const char *buf, int len)
{
    int i;
    (void)fd;
    for (i = 0; i < len; i++)
        LOGUART_PutChar((unsigned char)buf[i]);
    return len;
}

static char _quic_nl_heap[4096];
void *_sbrk(int incr)
{
    static char *cur = _quic_nl_heap;
    char *prev = cur;
    if (incr < 0 || cur + incr > _quic_nl_heap + sizeof(_quic_nl_heap))
        return (void *)-1;
    cur += incr;
    return prev;
}

int  _close(int fd)                  { (void)fd; return -1; }
int  _lseek(int fd, int off, int w)  { (void)fd; (void)off; (void)w; return 0; }
int  _read(int fd, char *buf, int l) { (void)fd; (void)buf; (void)l; return 0; }
int  _fstat(int fd, void *st)        { (void)fd; (void)st; return 0; }
int  _isatty(int fd)                 { (void)fd; return 1; }
int  _kill(int pid, int sig)         { (void)pid; (void)sig; return -1; }
int  _getpid(void)                   { return 1; }
void _exit(int code)                 { (void)code; for (;;) { } }
