/*
 * newlib_syscalls.c — cross-platform libc/newlib retarget, all in one place.
 *
 * Platform-neutral (component/syscalls): the LittleFS instance comes from the
 * vfs_lfs_mount() platform hook and the console fds route through the shared
 * dispatcher (component/console); no SoC headers here. Currently the AmebaD KM4
 * image is the only consumer.
 *
 * Groups, all force-linked into the image (strong defs that beat libc's
 * defaults) — see CMakeLists.txt:
 *
 *   1. POSIX fd VFS — a small file-descriptor layer so the newlib syscalls route
 *      by fd: 0/1/2 stay = console dispatcher; an _open()'d path -> a LittleFS file
 *      (vfs_lfs_mount + lfs_file_*); a socket registered via
 *      fd_open_socket() -> a FreeRTOS+TCP socket (FreeRTOS_recv/send/
 *      closesocket). fd -> slot is a hashtable index over a fixed slot pool.
 *
 *   2. newlib syscall entry points — _open/_close/_read/_write/_lseek/_fstat/
 *      _isatty/_stat/_unlink dispatch through (1); _sbrk + the inert POSIX stubs.
 *
 *   3. malloc family over the FreeRTOS heap (heap_5) — a STRONG malloc/free/calloc/
 *      realloc. The image is built -nostdlib; code that wants C malloc (ngtcp2's
 *      default ngtcp2_mem, littlefs's per-file cache, newlib snprintf, ...) must
 *      resolve it, and a strong malloc here also pre-empts newlib's own (which
 *      would grow through _sbrk). heap_5 has no realloc, so each block is prefixed
 *      with its size for copy-on-realloc.
 *
 *   4. BSD byte-order helpers — bare htonl/htons/ntohl/ntohs that socket code
 *      (ngtcp2_net.h, anything expecting <arpa/inet.h>) calls; they delegate to
 *      FreeRTOS+TCP's FreeRTOS_htonl/htons.
 *
 * The VFS + socket fd machinery is always compiled but per-function (the toolchain
 * builds with -ffunction-sections/-fdata-sections and the image links with
 * -Wl,--gc-sections — see cmake/arm-none-eabi.cmake), so when no consumer calls
 * fopen/open/fd_open_socket it is dead-stripped and costs nothing.
 *
 * GCC/newlib only (this project has no IAR/__ICCARM__ build).
 */
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "FreeRTOS.h"        /* pvPortMalloc / vPortFree (heap) + the mutex */
#include "semphr.h"
#include "FreeRTOS_IP.h"     /* FreeRTOS_htonl / FreeRTOS_htons (network byte order) */
#include "FreeRTOS_Sockets.h"/* Socket_t / FreeRTOS_recv / FreeRTOS_send / closesocket */
#include "lfs.h"             /* lfs_t / lfs_file_* / LFS_O_* / lfs_stat */
#include "console.h"         /* at_console_read/write — shared fd 0/1/2 dispatcher */
#include "syscalls.h"        /* vfs_lfs_mount() platform hook + fd_open_socket() */

/* Some platforms' SDK headers macro-redirect the malloc family to their heap
 * primitives (e.g. AmebaD platform_stdlib: `#define malloc pvPortMalloc` / free
 * vPortFree / ...). This TU includes no such header, but keep the #undefs as a
 * defensive guard so the STRONG symbols defined below are the real malloc/free/
 * calloc/realloc that third-party code (ngtcp2, littlefs, newlib) links against. */
#undef malloc
#undef free
#undef calloc
#undef realloc

/* ===== 1. POSIX fd VFS — slot pool + hashtable index ====================== *
 * fds 0/1/2 are the console and never enter the table. fds >= VFS_FD_BASE are
 * handed out by a monotonic allocator (so fd numbers are sparse/growing, not just
 * slot indices) and mapped fd -> slot through an open-addressing hashtable. The
 * slot pool holds the handle; a slot is a file XOR a socket, hence the union. The
 * per-file 512B lfs cache is NOT here — lfs_file_open mallocs it (heap_5) and
 * lfs_file_close frees it — so each slot is only ~sizeof(lfs_file_t). */
#define VFS_FD_BASE     3
#define VFS_MAX_FD      16          /* max concurrently-open file+socket fds */
#define VFS_HASH_SIZE   31          /* open-addressing buckets, prime > VFS_MAX_FD */

typedef enum { VFS_FREE = 0, VFS_FILE, VFS_SOCKET } vfs_type_t;

typedef struct {
    vfs_type_t type;                /* VFS_FREE => unused pool slot */
    union {
        lfs_file_t f;               /* VFS_FILE */
        Socket_t   s;               /* VFS_SOCKET */
    } u;
} vfs_slot_t;

typedef struct { int fd; int slot; } vfs_hentry_t;  /* fd == 0 => empty bucket */

static vfs_slot_t        s_slots[VFS_MAX_FD];
static vfs_hentry_t      s_htab[VFS_HASH_SIZE];
static int               s_next_fd = VFS_FD_BASE;   /* monotonic fd source */
static SemaphoreHandle_t s_fd_mutex;                /* lazily created */

static void fd_lock(void)
{
    if (s_fd_mutex == NULL) {
        s_fd_mutex = xSemaphoreCreateMutex();
    }
    if (s_fd_mutex != NULL) {
        xSemaphoreTake(s_fd_mutex, portMAX_DELAY);
    }
}

static void fd_unlock(void)
{
    if (s_fd_mutex != NULL) {
        xSemaphoreGive(s_fd_mutex);
    }
}

/* hashtable (caller holds fd_lock). h(fd) = fd % size, linear probing. */
static int htab_find(int fd)        /* -> bucket index, or -1 if absent */
{
    unsigned i = (unsigned)fd % VFS_HASH_SIZE;
    unsigned n;
    for (n = 0; n < VFS_HASH_SIZE; n++) {
        if (s_htab[i].fd == 0) {
            return -1;
        }
        if (s_htab[i].fd == fd) {
            return (int)i;
        }
        i = (i + 1) % VFS_HASH_SIZE;
    }
    return -1;
}

static int htab_insert(int fd, int slot)  /* 0 ok, -1 full */
{
    unsigned i = (unsigned)fd % VFS_HASH_SIZE;
    unsigned n;
    for (n = 0; n < VFS_HASH_SIZE; n++) {
        if (s_htab[i].fd == 0) {
            s_htab[i].fd = fd;
            s_htab[i].slot = slot;
            return 0;
        }
        i = (i + 1) % VFS_HASH_SIZE;
    }
    return -1;
}

/* Open-addressing backward-shift deletion from bucket b — keeps probe chains
 * intact without tombstones (the table is long-lived; tombstones would accrue). */
static void htab_remove_at(int b)
{
    unsigned i = (unsigned)b;
    unsigned j = i;
    s_htab[i].fd = 0;
    for (;;) {
        unsigned k;
        int move;
        j = (j + 1) % VFS_HASH_SIZE;
        if (s_htab[j].fd == 0) {
            break;
        }
        k = (unsigned)s_htab[j].fd % VFS_HASH_SIZE;
        /* move s_htab[j] into the hole at i iff its home k is NOT in (i, j] */
        move = (i <= j) ? (k <= i || k > j) : (k <= i && k > j);
        if (move) {
            s_htab[i] = s_htab[j];
            s_htab[j].fd = 0;
            i = j;
        }
    }
}

/* Allocate a free pool slot + a fresh fd. Returns fd (>=VFS_FD_BASE) and *out, or
 * -1 (errno=EMFILE). */
static int fd_alloc(vfs_type_t type, vfs_slot_t **out)
{
    int slot = -1, fd, i;

    fd_lock();
    for (i = 0; i < VFS_MAX_FD; i++) {
        if (s_slots[i].type == VFS_FREE) { slot = i; break; }
    }
    if (slot < 0) {
        fd_unlock();
        errno = EMFILE;
        return -1;
    }
    for (;;) {
        fd = s_next_fd++;
        if (s_next_fd > 0x7FFFFFF0) {
            s_next_fd = VFS_FD_BASE;
        }
        if (fd >= VFS_FD_BASE && htab_find(fd) < 0) {
            break;
        }
    }
    if (htab_insert(fd, slot) < 0) {
        fd_unlock();
        errno = EMFILE;
        return -1;
    }
    s_slots[slot].type = type;
    if (out != NULL) {
        *out = &s_slots[slot];
    }
    fd_unlock();
    return fd;
}

/* fd -> slot (NULL + errno=EBADF if not an open VFS fd). Caller must not be inside
 * fd_lock. The returned slot stays valid until its fd is closed. */
static vfs_slot_t *fd_get(int fd)
{
    vfs_slot_t *s = NULL;
    int b;

    fd_lock();
    b = htab_find(fd);
    if (b >= 0) {
        s = &s_slots[s_htab[b].slot];
    }
    fd_unlock();
    if (s == NULL) {
        errno = EBADF;
    }
    return s;
}

static void fd_free(int fd)
{
    int b;
    fd_lock();
    b = htab_find(fd);
    if (b >= 0) {
        s_slots[s_htab[b].slot].type = VFS_FREE;
        htab_remove_at(b);
    }
    fd_unlock();
}

/* Backend op tables. The console syscalls (_read/_write/_close/_lseek/_fstat) are
 * kept by libc (stdio pulls them), and a direct reference to the lfs_file_ and
 * FreeRTOS_ backends from them would drag littlefs + the ~45KB FreeRTOS+TCP sockets
 * module into EVERY image even when nothing opens a file or registers a socket. So
 * the file/socket backends are reached only through these NULL-until-wired pointers;
 * they are wired by _open()/fd_open_socket(), which are themselves
 * dead-strippable. When no consumer calls those, the op-table impls (and their lfs /
 * FreeRTOS refs) are unreferenced and --gc-sections drops them — zero cost unused. */
typedef struct {
    int  (*read )(vfs_slot_t *s, char *buf, int len);
    int  (*write)(vfs_slot_t *s, const char *buf, int len);
    int  (*close)(vfs_slot_t *s);
    int  (*lseek)(vfs_slot_t *s, int off, int whence);
    long (*size )(vfs_slot_t *s);          /* for _fstat; <0 on error */
} vfs_ops_t;
static const vfs_ops_t *s_file_ops;        /* set by _open */
static const vfs_ops_t *s_sock_ops;        /* set by fd_open_socket */

/* errno mappers */
static int lfs_to_errno(int e)
{
    switch (e) {
    case LFS_ERR_OK:          return 0;
    case LFS_ERR_NOENT:       return ENOENT;
    case LFS_ERR_EXIST:       return EEXIST;
    case LFS_ERR_NOTDIR:      return ENOTDIR;
    case LFS_ERR_ISDIR:       return EISDIR;
    case LFS_ERR_NOTEMPTY:    return ENOTEMPTY;
    case LFS_ERR_BADF:        return EBADF;
    case LFS_ERR_FBIG:        return EFBIG;
    case LFS_ERR_INVAL:       return EINVAL;
    case LFS_ERR_NOSPC:       return ENOSPC;
    case LFS_ERR_NOMEM:       return ENOMEM;
    case LFS_ERR_NAMETOOLONG: return ENAMETOOLONG;
    case LFS_ERR_IO:
    case LFS_ERR_CORRUPT:
    default:                  return EIO;
    }
}

/* FreeRTOS+TCP returns negative -pdFREERTOS_ERRNO_* whose magnitudes match POSIX
 * (see FreeRTOS_errno_TCP.h); a bare FREERTOS_SOCKET_ERROR(-1) falls back to EIO. */
static int frtos_to_errno(BaseType_t r)
{
    int e = (int)(-r);
    return (e > 1) ? e : EIO;
}

/* ---- LittleFS file backend (wired by _open) ---- */
static int file_read(vfs_slot_t *s, char *b, int n)
{
    lfs_ssize_t r = lfs_file_read(vfs_lfs_mount(), &s->u.f, b, (lfs_size_t)n);
    return (r < 0) ? (errno = lfs_to_errno((int)r), -1) : (int)r;
}
static int file_write(vfs_slot_t *s, const char *b, int n)
{
    lfs_ssize_t r = lfs_file_write(vfs_lfs_mount(), &s->u.f, b, (lfs_size_t)n);
    return (r < 0) ? (errno = lfs_to_errno((int)r), -1) : (int)r;
}
static int file_close(vfs_slot_t *s)
{
    int r = lfs_file_close(vfs_lfs_mount(), &s->u.f);
    return (r < 0) ? (errno = lfs_to_errno(r), -1) : 0;
}
static int file_lseek(vfs_slot_t *s, int off, int whence)
{
    /* SEEK_SET/CUR/END (0/1/2) == LFS_SEEK_SET/CUR/END */
    lfs_soff_t p = lfs_file_seek(vfs_lfs_mount(), &s->u.f, off, whence);
    return (p < 0) ? (errno = lfs_to_errno((int)p), -1) : (int)p;
}
static long file_size(vfs_slot_t *s)
{
    return (long)lfs_file_size(vfs_lfs_mount(), &s->u.f);
}
static const vfs_ops_t FILE_OPS = { file_read, file_write, file_close, file_lseek, file_size };

/* ---- FreeRTOS+TCP socket backend (wired by fd_open_socket) ---- */
static int sock_read(vfs_slot_t *s, char *b, int n)
{
    BaseType_t r = FreeRTOS_recv(s->u.s, b, (size_t)n, 0);
    return (r < 0) ? (errno = frtos_to_errno(r), -1) : (int)r;
}
static int sock_write(vfs_slot_t *s, const char *b, int n)
{
    BaseType_t r = FreeRTOS_send(s->u.s, b, (size_t)n, 0);
    return (r < 0) ? (errno = frtos_to_errno(r), -1) : (int)r;
}
static int sock_close(vfs_slot_t *s)
{
    BaseType_t r = FreeRTOS_closesocket(s->u.s);
    return (r < 0) ? (errno = frtos_to_errno(r), -1) : 0;
}
static int sock_lseek(vfs_slot_t *s, int off, int whence)
{
    (void)s; (void)off; (void)whence; errno = ESPIPE; return -1;
}
static long sock_size(vfs_slot_t *s) { (void)s; return -1; }
static const vfs_ops_t SOCK_OPS = { sock_read, sock_write, sock_close, sock_lseek, sock_size };

/* Register an already-created FreeRTOS socket as a POSIX fd, so read()/write()/
 * close() on the returned fd dispatch to FreeRTOS_recv/send/closesocket. Socket
 * creation/options stay on the native FreeRTOS_* API (as quic_test.c/iperf do).
 * The handle is opaque (void *) at the public boundary (syscalls.h) so callers that
 * only register a socket need not match this TU's FreeRTOS+TCP headers. */
int fd_open_socket(void *socket)
{
    Socket_t s = (Socket_t)socket;
    vfs_slot_t *slot;
    int fd;

    if (s == NULL || s == FREERTOS_INVALID_SOCKET) {
        errno = EINVAL;
        return -1;
    }
    fd = fd_alloc(VFS_SOCKET, &slot);
    if (fd < 0) {
        return -1;   /* errno set */
    }
    slot->u.s = s;
    s_sock_ops = &SOCK_OPS;   /* wires the socket backend (-> FreeRTOS_recv/send/...) */
    return fd;
}

/* fd -> its backend op table (NULL only if the fd's backend was never wired, which
 * for a valid open fd cannot happen — _open/fd_open_socket set it). */
static const vfs_ops_t *slot_ops(const vfs_slot_t *s)
{
    return (s->type == VFS_FILE) ? s_file_ops : s_sock_ops;
}

/* ===== 2. newlib syscall entry points ==================================== */

/* Console fds 0/1/2 dispatch to the cross-platform console component
 * (component/console). The byte I/O backend (LOGUART by default, LPUART when
 * console_lpuart.c registers it) lives there; this file only routes the fds.
 * See console_loguart.c (default backend) / console_lpuart.c (override). */

int _open(const char *path, int flags, int mode)
{
    lfs_t *lfs;
    vfs_slot_t *slot;
    int lflags = 0, fd, rc;

    (void)mode;
    lfs = vfs_lfs_mount();
    if (lfs == NULL) {
        errno = ENODEV;
        return -1;
    }
    switch (flags & O_ACCMODE) {
    case O_RDONLY: lflags = LFS_O_RDONLY; break;
    case O_WRONLY: lflags = LFS_O_WRONLY; break;
    case O_RDWR:   lflags = LFS_O_RDWR;   break;
    default:       errno = EINVAL; return -1;
    }
    if (flags & O_CREAT)  lflags |= LFS_O_CREAT;
    if (flags & O_EXCL)   lflags |= LFS_O_EXCL;
    if (flags & O_TRUNC)  lflags |= LFS_O_TRUNC;
    if (flags & O_APPEND) lflags |= LFS_O_APPEND;

    fd = fd_alloc(VFS_FILE, &slot);
    if (fd < 0) {
        return -1;   /* errno=EMFILE */
    }
    rc = lfs_file_open(lfs, &slot->u.f, path, lflags);
    if (rc < 0) {
        fd_free(fd);
        errno = lfs_to_errno(rc);
        return -1;
    }
    s_file_ops = &FILE_OPS;   /* wires the file backend (-> lfs_file_*) */
    return fd;
}

int _close(int fd)
{
    vfs_slot_t *s;
    const vfs_ops_t *ops;
    int rc;

    if (fd <= 2) {
        return 0;
    }
    s = fd_get(fd);
    if (s == NULL) {
        return -1;
    }
    ops = slot_ops(s);
    rc = ops ? ops->close(s) : (errno = EBADF, -1);
    fd_free(fd);
    return rc;
}

int _read(int fd, char *buf, int len)
{
    vfs_slot_t *s;
    const vfs_ops_t *ops;

    if (fd <= 2) {                       /* console -> shared dispatcher */
        return at_console_read(buf, len);
    }
    s = fd_get(fd);
    if (s == NULL) {
        return -1;
    }
    ops = slot_ops(s);
    return ops ? ops->read(s, buf, len) : (errno = EBADF, -1);
}

int _write(int fd, char *buf, int len)
{
    vfs_slot_t *s;
    const vfs_ops_t *ops;

    if (fd <= 2) {                       /* console -> shared dispatcher */
        return at_console_write(fd, buf, len);
    }
    s = fd_get(fd);
    if (s == NULL) {
        return -1;
    }
    ops = slot_ops(s);
    return ops ? ops->write(s, buf, len) : (errno = EBADF, -1);
}

int _lseek(int fd, int off, int whence)
{
    vfs_slot_t *s;
    const vfs_ops_t *ops;

    if (fd <= 2) {
        return 0;                        /* console: unseekable, benign */
    }
    s = fd_get(fd);
    if (s == NULL) {
        return -1;
    }
    ops = slot_ops(s);
    return ops ? ops->lseek(s, off, whence) : (errno = EBADF, -1);
}

int _fstat(int fd, struct stat *st)
{
    vfs_slot_t *s;
    const vfs_ops_t *ops;

    memset(st, 0, sizeof(*st));
    if (fd <= 2) {
        st->st_mode = S_IFCHR;
        return 0;
    }
    s = fd_get(fd);
    if (s == NULL) {
        return -1;
    }
    if (s->type == VFS_FILE) {
        long sz;
        st->st_mode = S_IFREG;
        ops = slot_ops(s);
        sz = ops ? ops->size(s) : -1;
        st->st_size = (sz < 0) ? 0 : sz;
    } else {
        st->st_mode = S_IFSOCK;
    }
    return 0;
}

int _isatty(int fd)
{
    return (fd <= 2) ? 1 : 0;
}

int _stat(const char *path, struct stat *st)
{
    struct lfs_info info;
    int rc = lfs_stat(vfs_lfs_mount(), path, &info);

    if (rc < 0) {
        errno = lfs_to_errno(rc);
        return -1;
    }
    memset(st, 0, sizeof(*st));
    st->st_mode = (info.type == LFS_TYPE_DIR) ? S_IFDIR : S_IFREG;
    st->st_size = info.size;
    return 0;
}

int _unlink(const char *path)
{
    int rc = lfs_remove(vfs_lfs_mount(), path);
    return (rc < 0) ? (errno = lfs_to_errno(rc), -1) : 0;
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

int  _kill(int pid, int sig)         { (void)pid; (void)sig; return -1; }
int  _getpid(void)                   { return 1; }
void _exit(int code)                 { (void)code; for (;;) { } }

/* ---------------------------------------------------------------------------
 * Remaining newlib syscall surface — inert. No current image references these;
 * un-#if-0 + implement when a consumer pulls one (e.g. SNTP would make
 * _gettimeofday return real time instead of -1).
 * ------------------------------------------------------------------------- */
#if 0
int    _link(const char *a, const char *b)          { (void)a; (void)b; return -1; }
int    _times(void *buf)                            { (void)buf; return -1; }
int    _gettimeofday(void *tv, void *tz)            { (void)tv; (void)tz; return -1; }
int    _fork(void)                                  { return -1; }
int    _execve(const char *p, char *const a[], char *const e[]) { (void)p; (void)a; (void)e; return -1; }
int    _wait(int *status)                           { (void)status; return -1; }
char  *__env[1] = { 0 };
char **environ  = __env;
#endif

/* ===== 3. malloc family over the FreeRTOS heap =========================== *
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

/* newlib's stdio (fopen/__sfp/stream buffers) and libstdc++ allocate through the
 * REENTRANT allocator _malloc_r — NOT the plain malloc above. Left to newlib's
 * default, _malloc_r grows the tiny _sbrk arena (4 KB) and runs out (ENOMEM) the
 * moment a C++ ofstream/filebuf buffer is needed. Route the whole _*_r family to
 * the same heap_5-backed malloc so newlib internals share the real heap. The
 * reent arg is unused (our allocator is task-safe via the FreeRTOS heap lock). */
struct _reent;
void *_malloc_r (struct _reent *r, size_t n)            { (void)r; return malloc(n); }
void  _free_r   (struct _reent *r, void *p)             { (void)r; free(p); }
void *_calloc_r (struct _reent *r, size_t a, size_t b)  { (void)r; return calloc(a, b); }
void *_realloc_r(struct _reent *r, void *p, size_t n)   { (void)r; return realloc(p, n); }

/* ===== 4. BSD byte-order helpers (delegate to FreeRTOS+TCP) =============== *
 * ngtcp2_net.h's non-WIN32 path expands ngtcp2_htonl(N) -> htonl(N) etc., and it
 * does not include <arpa/inet.h> (HAVE_ARPA_INET_H is unset), so it needs these
 * as real symbols. Bodies reuse FreeRTOS+TCP's FreeRTOS_htonl/htons macros rather
 * than open-coding the swap. */
uint32_t htonl(uint32_t x) { return FreeRTOS_htonl(x); }
uint16_t htons(uint16_t x) { return FreeRTOS_htons(x); }
uint32_t ntohl(uint32_t x) { return FreeRTOS_ntohl(x); }
uint16_t ntohs(uint16_t x) { return FreeRTOS_ntohs(x); }
