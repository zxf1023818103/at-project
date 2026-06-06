/*
 * hci_syscalls.c — minimal newlib syscall retarget for the BLE-DTM image.
 *
 * The vendored BT source pulls newlib's reentrant stdio (via _rtl_printf and
 * abort), which needs _write/_sbrk/_exit/... The base WiFi image is
 * newlib-stdio-free (uses DiagPrintf), so these aren't provided elsewhere.
 *   _write -> LOGUART  (so any newlib-routed BT print still reaches COM29)
 *   _sbrk  -> a small static heap for newlib's printf buffering
 *   rest   -> inert stubs
 * Only linked into the BT-DTM image (part of amebad_hci).
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

static char _bt_nl_heap[4096];
void *_sbrk(int incr)
{
    static char *cur = _bt_nl_heap;
    char *prev = cur;
    if (incr < 0 || cur + incr > _bt_nl_heap + sizeof(_bt_nl_heap))
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
