/* All hostap callbacks and timers run on the NAN task. No socket event loop. */
#include "utils/includes.h"
#include "utils/common.h"
#include "utils/eloop.h"
#include "FreeRTOS.h"
#include "task.h"

extern int green2_nan_random(void *, size_t);
int os_get_random(unsigned char *out, size_t len) { return green2_nan_random(out, len); }
unsigned long os_random(void)
{ unsigned long word = 0; if (green2_nan_random(&word, sizeof(word))) abort(); return word; }
void *os_memdup(const void *p, size_t len) { void *out = malloc(len); if (out) memcpy(out, p, len); return out; }
void *os_zalloc(size_t len) { return calloc(1, len); }
int os_memcmp_const(const void *a, const void *b, size_t len)
{ const unsigned char *x = a, *y = b; unsigned char d = 0; while (len--) d |= *x++ ^ *y++; return d; }
size_t os_strlcpy(char *dst, const char *src, size_t len)
{ size_t n = strlen(src); if (len) { size_t c = n < len - 1 ? n : len - 1; memcpy(dst, src, c); dst[c] = 0; } return n; }
/* Called by one task; extend the tick counter across wrap without wall clock. */
static uint64_t monotonic_us(void)
{
    static TickType_t last;
    static uint64_t ticks;
    TickType_t now = xTaskGetTickCount();
    ticks += (TickType_t)(now - last); last = now;
    return ticks * 1000000 / configTICK_RATE_HZ;
}
int os_get_reltime(struct os_reltime *t)
{ uint64_t us = monotonic_us(); t->sec = us / 1000000; t->usec = us % 1000000; return 0; }
int os_get_time(struct os_time *t)
{ struct timeval tv; if (gettimeofday(&tv, NULL)) return -1; t->sec = tv.tv_sec; t->usec = tv.tv_usec; return 0; }
void os_sleep(os_time_t sec, os_time_t us)
{ vTaskDelay(pdMS_TO_TICKS(sec * 1000 + (us + 999) / 1000)); }

struct timeout { struct timeout *next; uint64_t deadline; eloop_timeout_handler handler; void *eloop, *user; };
static struct timeout *timers;
int eloop_register_timeout(unsigned int sec, unsigned int us, eloop_timeout_handler handler, void *eloop, void *user)
{
    struct timeout *t = os_zalloc(sizeof(*t));
    if (!t) return -1;
    t->deadline = monotonic_us() + (uint64_t)sec * 1000000 + us;
    t->handler = handler; t->eloop = eloop; t->user = user; t->next = timers; timers = t;
    return 0;
}
int eloop_cancel_timeout(eloop_timeout_handler handler, void *eloop, void *user)
{
    int count = 0; struct timeout **p = &timers;
    while (*p) {
        struct timeout *t = *p;
        if (t->handler == handler && (eloop == ELOOP_ALL_CTX || eloop == t->eloop) && (user == ELOOP_ALL_CTX || user == t->user)) {
            *p = t->next; free(t); count++;
        } else p = &t->next;
    }
    return count;
}
int eloop_is_timeout_registered(eloop_timeout_handler handler, void *eloop, void *user)
{ for (struct timeout *t = timers; t; t = t->next) if (t->handler == handler && t->eloop == eloop && t->user == user) return 1; return 0; }
unsigned green2_pasn_next_timeout_ms(unsigned max_wait_ms)
{
    uint64_t now = monotonic_us();
    for (struct timeout *t = timers; t; t = t->next) {
        if (t->deadline <= now) return 0;
        /* Round up: a sub-millisecond remainder must not cause a busy loop. */
        uint64_t remaining = (t->deadline - now + 999) / 1000;
        if (remaining < max_wait_ms) max_wait_ms = (unsigned)remaining;
    }
    return max_wait_ms;
}
void green2_pasn_timers(void)
{
    uint64_t now = monotonic_us(); struct timeout **p = &timers;
    while (*p) {
        struct timeout *t = *p;
        if (now < t->deadline) { p = &t->next; continue; }
        *p = t->next; eloop_timeout_handler handler = t->handler; void *e = t->eloop, *u = t->user;
        free(t); handler(e, u); p = &timers;
    }
}
