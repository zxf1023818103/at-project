/*
 * picolibc stdio bridge.
 *
 * Only built when the active toolchain's C library is picolibc — today that is
 * bl618dg alone, which MUST build with the Zephyr SDK (GCC 14): Bouffalo ships
 * that chip's macsw / macsw_config / wl80211 wireless libraries as slim LTO
 * bytecode (.text is 0 bytes), so no other compiler can link them. Every other
 * platform here uses newlib, whose stdio is wired through _read/_write in
 * newlib_syscalls.c and needs none of this.
 *
 * newlib defines stdin/stdout/stderr itself (via _impure_ptr) and reaches the
 * console through _write(). picolibc's tinystdio does NOT: it leaves the three
 * FILE objects for the application to define, which is why linking against it
 * otherwise fails with "undefined reference to `stdout'".
 *
 * So define them here over the SAME console path newlib_syscalls.c uses
 * (at_console_write / _read), keeping console behaviour identical across
 * platforms.
 */
#include <stdio.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

/* Shared console dispatcher — same one _write() in newlib_syscalls.c uses. */
extern int at_console_write(int fd, const char *buf, int len);
extern int _read(int fd, char *buf, int len);

/* ---------------------------------------------------------------------------
 * Thread-local storage.
 *
 * The third — and least obvious — newlib/picolibc divergence: picolibc puts
 * `errno` and `_localtime_buf` in TLS, addressed off the RISC-V thread pointer
 * (tp/x4). Nothing in this project ever set tp (newlib needs no TLS), so tp
 * reads 0 and the first TLS write faults. It presents as a Store access fault
 * with mtval ≈ the member offset — e.g. localtime() compiles to
 * `addi a1,tp,12` (_localtime_buf) and then stores tm_year at 0x14.
 *
 * Give libc one TLS block for the whole image. All FreeRTOS tasks share it, so
 * errno is global rather than per-task — which matches the newlib configuration
 * the other platforms already run (single reent, shared errno), so behaviour
 * stays consistent across the port.
 *
 * Called explicitly from main() via at_libc_early_init(): this bare-metal
 * startup has no .init_array and never calls __libc_init_array, so a
 * __attribute__((constructor)) would silently never run (verified: the symbol
 * links in, but nothing calls it and the fault persists).
 * ------------------------------------------------------------------------- */
#include <picotls.h>

/* Sized generously; _tls_size() is a link-time value and cannot size an array.
 * The constructor below hard-fails if the real block ever outgrows it. */
static char at_tls_block[512] __attribute__((aligned(16)));

void at_libc_early_init(void)
{
    if (_tls_size() > sizeof(at_tls_block)) {
        /* Cannot printf reliably before TLS exists — halt loudly instead of
         * corrupting memory past the block. */
        for (;;) {
        }
    }
    _init_tls(at_tls_block);
    _set_tls(at_tls_block);
}

static int at_stdio_put(char c, FILE *file)
{
    (void)file;
    /* fd 1 = stdout; the dispatcher treats 0/1/2 alike. */
    return at_console_write(1, &c, 1) == 1 ? (unsigned char)c : -1;
}

static int at_stdio_get(FILE *file)
{
    char c;

    (void)file;
    if (_read(STDIN_FILENO, &c, 1) != 1) {
        return _FDEV_EOF;
    }
    return (unsigned char)c;
}

static FILE __at_stdio = FDEV_SETUP_STREAM(at_stdio_put, at_stdio_get, NULL,
                                           _FDEV_SETUP_RW);

/* picolibc declares these as `FILE *const` (stdio.h) — one shared bidirectional
 * stream is enough; the console is a single UART. */
FILE *const stdin = &__at_stdio;
FILE *const stdout = &__at_stdio;
FILE *const stderr = &__at_stdio;

/* ---------------------------------------------------------------------------
 * POSIX syscall names.
 *
 * The second newlib/picolibc divergence: newlib's libc calls the reentrant
 * `_`-prefixed syscalls (_open/_close/_read/...), which newlib_syscalls.c
 * defines. picolibc's libc calls the PLAIN POSIX names instead, so those come
 * out undefined at link. Forward them to the existing implementations rather
 * than duplicating the VFS — one definition, both naming conventions.
 * ------------------------------------------------------------------------- */
extern int _open(const char *path, int flags, int mode);
extern int _close(int fd);
extern int _write(int fd, char *buf, int len);
extern int _lseek(int fd, int off, int whence);
extern int _stat(const char *path, struct stat *st);
extern int _unlink(const char *path);
extern int _gettimeofday(struct timeval *tv, void *tz);

int open(const char *path, int flags, ...)
{
    /* The mode argument only matters for O_CREAT; the VFS ignores it. */
    return _open(path, flags, 0);
}

int close(int fd)                              { return _close(fd); }
ssize_t read(int fd, void *buf, size_t len)    { return _read(fd, (char *)buf, (int)len); }
ssize_t write(int fd, const void *buf, size_t len)
{
    /* _write takes a non-const char* (newlib's signature); the VFS never writes
     * through it. */
    return _write(fd, (char *)(uintptr_t)buf, (int)len);
}
off_t lseek(int fd, off_t off, int whence)     { return _lseek(fd, (int)off, whence); }
int stat(const char *path, struct stat *st)    { return _stat(path, st); }
int unlink(const char *path)                   { return _unlink(path); }
int gettimeofday(struct timeval *tv, void *tz) { return _gettimeofday(tv, tz); }
