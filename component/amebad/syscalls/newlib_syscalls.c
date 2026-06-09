/*
 * newlib_syscalls.c — the AmebaD KM4 image's libc/newlib retarget, all in one place.
 *
 * Three groups, all force-linked into the KM4 image (strong defs that beat libc's
 * defaults) — see CMakeLists.txt:
 *
 *   1. newlib syscall stubs — consolidated from the former three copies
 *      (hci/hci_syscalls.c, app_km4/quic_syscalls.c, monitor/low_level_io.c):
 *        _write -> LOGUART console (DiagPutChar), so any newlib-routed print still
 *                  lands on COM3; fd is ignored on purpose (BT/TLS use various fds).
 *        _read  -> line input from the console (DiagGetChar), for the monitor shell.
 *        _sbrk  -> a small static heap for newlib's stdio buffering (NOT the app heap).
 *        rest   -> inert POSIX stubs newlib expects to exist.
 *
 *   2. malloc family over the FreeRTOS heap (heap_5) — a STRONG malloc/free/calloc/
 *      realloc. The image is built -nostdlib; code that wants C malloc (ngtcp2's
 *      default ngtcp2_mem, newlib snprintf, ...) must resolve it, and a strong
 *      malloc here also pre-empts newlib's own (which would grow through _sbrk).
 *      heap_5 has no realloc, so each block is prefixed with its size for
 *      copy-on-realloc. (Moved here from the old ngtcp2/ngtcp2_port.c so every KM4
 *      program shares one copy, not just QUIC.)
 *
 *   3. BSD byte-order helpers — bare htonl/htons/ntohl/ntohs that socket code
 *      (ngtcp2_net.h, anything expecting <arpa/inet.h>) calls. They do NOT
 *      re-implement the swap: they delegate to FreeRTOS+TCP's FreeRTOS_htonl/htons
 *      so there is a single source of truth for the project's network byte order.
 *
 * GCC/newlib only (this project has no IAR/__ICCARM__ build).
 */
#include "ameba_soc.h"       /* DiagPutChar / DiagGetChar (ROM console) */
#include <string.h>
#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"        /* pvPortMalloc / vPortFree (heap) */
#include "FreeRTOS_IP.h"     /* FreeRTOS_htonl / FreeRTOS_htons (network byte order) */

/* platform_stdlib (pulled in by ameba_soc.h) macro-redirects the malloc family to
 * the SDK primitives: `#define malloc pvPortMalloc` / free vPortFree / calloc
 * rtw_calloc / realloc pvPortReAlloc. Undo them here so the STRONG symbols defined
 * below are the real malloc/free/calloc/realloc that third-party code (ngtcp2,
 * newlib) links against — not redefinitions of the pvPort / rtw primitives. */
#undef malloc
#undef free
#undef calloc
#undef realloc

/* ===== 1. newlib syscall stubs =========================================== */

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

/* ===== 2. malloc family over the FreeRTOS heap =========================== *
 * 8-byte header keeps the returned payload 8-byte aligned (FreeRTOS heap returns
 * 8-byte-aligned blocks; portBYTE_ALIGNMENT is 8 on Cortex-M). */
typedef struct {
    size_t size;
    size_t _pad;
} mem_hdr_t;

void *malloc(size_t size)
{
    mem_hdr_t *h;

    if (size == 0) {
        size = 1;
    }
    h = (mem_hdr_t *)pvPortMalloc(sizeof(mem_hdr_t) + size);
    if (h == NULL) {
        return NULL;
    }
    h->size = size;
    return (void *)(h + 1);
}

void free(void *ptr)
{
    if (ptr == NULL) {
        return;
    }
    vPortFree((void *)(((mem_hdr_t *)ptr) - 1));
}

void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void *p;

    /* overflow guard */
    if (nmemb != 0 && total / nmemb != size) {
        return NULL;
    }
    p = malloc(total);
    if (p != NULL) {
        memset(p, 0, total);
    }
    return p;
}

void *realloc(void *ptr, size_t size)
{
    mem_hdr_t *h;
    size_t old;
    void *np;

    if (ptr == NULL) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    h = ((mem_hdr_t *)ptr) - 1;
    old = h->size;
    np = malloc(size);
    if (np == NULL) {
        return NULL;
    }
    memcpy(np, ptr, old < size ? old : size);
    free(ptr);
    return np;
}

/* ===== 3. BSD byte-order helpers (delegate to FreeRTOS+TCP) =============== *
 * ngtcp2_net.h's non-WIN32 path expands ngtcp2_htonl(N) -> htonl(N) etc., and it
 * does not include <arpa/inet.h> (HAVE_ARPA_INET_H is unset), so it needs these
 * as real symbols. Bodies reuse FreeRTOS+TCP's FreeRTOS_htonl/htons macros rather
 * than open-coding the swap. */
uint32_t htonl(uint32_t x) { return FreeRTOS_htonl(x); }
uint16_t htons(uint16_t x) { return FreeRTOS_htons(x); }
uint32_t ntohl(uint32_t x) { return FreeRTOS_ntohl(x); }
uint16_t ntohs(uint16_t x) { return FreeRTOS_ntohs(x); }
