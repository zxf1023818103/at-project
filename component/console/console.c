/*
 * console.c — the cross-platform stdio (fd 0/1/2) dispatcher. See console.h.
 *
 * Holds the active backend pointer + the shared line discipline. No platform
 * dependencies: every byte goes through the registered backend's putc/getc, so
 * this same object serves AmebaD, Bouffalo, and any other newlib retarget.
 */
#include "console.h"

/* Per-platform zero-config default. Weak here so a platform can provide a strong
 * override (returning its LOGUART / bflb / ... backend) without a runtime call. */
#if defined(__GNUC__)
__attribute__((weak))
#endif
const at_console_backend_t *at_console_default_backend(void)
{
    return 0;
}

static const at_console_backend_t *s_be;

/* Weak no-op: platforms without a clock get untimestamped logs (unchanged). The
 * time module (e.g. syscall_config/time_base.c) provides a strong override. */
#if defined(__GNUC__)
__attribute__((weak))
#endif
int at_console_timestamp(char *buf, int cap)
{
    (void) buf;
    (void) cap;
    return 0;
}

void at_console_set_backend(const at_console_backend_t *be)
{
    s_be = be;
}

/* Resolve the active backend, falling back to the platform default on first use. */
static const at_console_backend_t *cur(void)
{
    if (s_be == 0) {
        s_be = at_console_default_backend();
    }
    return s_be;
}

int at_console_write(int fd, const char *buf, int len)
{
    /* Next byte begins a fresh line -> prepend the timestamp. Static so it
     * survives the fragmentation of one logical line across many _write calls
     * (newlib often emits a printf char-by-char). */
    static int at_line_start = 1;
    const at_console_backend_t *be = cur();
    void (*put)(char);
    int i;

    if (be == 0) {
        return len;                 /* no console wired: sink the bytes */
    }
    /* stderr -> dedicated device when provided, else fall back to putc. */
    put = (fd == 2 && be->errputc != 0) ? be->errputc : be->putc;
    if (put == 0) {
        return len;
    }
    for (i = 0; i < len; i++) {
        char c = buf[i];
        /* Stamp at the first real character of a line (not on a leading CR/LF,
         * so blank lines stay blank). */
        if (at_line_start && c != '\n' && c != '\r') {
            char ts[32];
            int n = at_console_timestamp(ts, (int) sizeof ts);
            int k;
            for (k = 0; k < n && k < (int) sizeof ts; k++) {
                put(ts[k]);
            }
            at_line_start = 0;
        }
        put(c);
        if (c == '\n') {
            at_line_start = 1;
        }
    }
    return len;
}

void at_console_write_raw(const char *buf, int len)
{
    const at_console_backend_t *be = cur();
    int i;

    if (be == 0 || be->putc == 0) {
        return;                     /* no console wired: sink the bytes */
    }
    for (i = 0; i < len; i++) {
        be->putc(buf[i]);
    }
}

int at_console_getc(void)
{
    const at_console_backend_t *be = cur();

    if (be == 0 || be->getc == 0) {
        return -1;                  /* no input device */
    }
    return be->getc();
}

int at_console_read(char *buf, int len)
{
    const at_console_backend_t *be = cur();
    void (*echo)(char) = (be != 0) ? be->putc : 0;
    int n = 0;

    if (be == 0 || be->getc == 0) {
        return 0;                   /* no input device */
    }
    for (; len > 0; --len) {
        int c = be->getc();
        if (c < 0) {
            break;
        }
        if (c == '\r' || c == '\n') {       /* Enter ends the line (canonical) */
            if (echo != 0) { echo('\r'); echo('\n'); }
            *buf++ = '\n';                   /* deliver '\n' so getline/cin finish */
            ++n;
            break;
        }
        if (echo != 0) { echo((char)c); }    /* echo the typed char */
        *buf++ = (char)c;
        ++n;
    }
    return n;
}
