#include "utils/includes.h"
#include "utils/common.h"
#include "utils/eloop.h"
#include "nan_pasn.h"
#include "FreeRTOS.h"
#include <assert.h>

static TickType_t now;
static unsigned fired;
TickType_t xTaskGetTickCount(void) { return now; }
void vTaskDelay(TickType_t ticks) { now += ticks; }
int green2_nan_random(void *p, size_t n) { (void)p; (void)n; return -1; }
static void count(void *eloop, void *user)
{ (void)eloop; (void)user; ++fired; }
static void rearm(void *eloop, void *user)
{
    (void)eloop; (void)user; ++fired;
    assert(eloop_register_timeout(0, 2000, count, NULL, NULL) == 0);
}

int main(void)
{
    assert(green2_pasn_next_timeout_ms(10000) == 10000);
    /* Insertion order is not deadline order; cancellation changes the wait. */
    assert(eloop_register_timeout(0, 50000, count, NULL, (void *)1) == 0);
    assert(eloop_register_timeout(0, 100000, count, NULL, (void *)2) == 0);
    assert(green2_pasn_next_timeout_ms(10000) == 50);
    assert(green2_pasn_next_timeout_ms(10) == 10);
    assert(eloop_cancel_timeout(count, NULL, (void *)1) == 1);
    assert(green2_pasn_next_timeout_ms(10000) == 100);
    now = 100;
    assert(green2_pasn_next_timeout_ms(10000) == 0);
    green2_pasn_timers();
    assert(fired == 1 && green2_pasn_next_timeout_ms(10000) == 10000);

    /* Round fractional milliseconds up and never execute before the deadline. */
    assert(eloop_register_timeout(0, 1001, count, NULL, NULL) == 0);
    assert(green2_pasn_next_timeout_ms(10000) == 2);
    ++now; green2_pasn_timers(); assert(fired == 1);
    assert(green2_pasn_next_timeout_ms(10000) == 1);
    ++now; green2_pasn_timers(); assert(fired == 2);

    /* The monotonic extension must keep a deadline valid over tick wrap. */
    now = UINT32_MAX - 2;
    assert(eloop_register_timeout(0, 5000, rearm, NULL, NULL) == 0);
    assert(green2_pasn_next_timeout_ms(10000) == 5);
    now = 1; green2_pasn_timers(); assert(fired == 2);
    assert(green2_pasn_next_timeout_ms(10000) == 1);
    now = 2; green2_pasn_timers(); assert(fired == 3);
    assert(green2_pasn_next_timeout_ms(10000) == 2);
    now = 4; green2_pasn_timers(); assert(fired == 4);
    assert(green2_pasn_next_timeout_ms(10000) == 10000);

    /* Long cache lifetimes must not overflow the wait conversion. */
    assert(eloop_register_timeout(UINT32_MAX, 999999, count, NULL, NULL) == 0);
    assert(green2_pasn_next_timeout_ms(10000) == 10000);
    assert(eloop_cancel_timeout(count, ELOOP_ALL_CTX, ELOOP_ALL_CTX) == 1);
    puts("PASN deadline, cancellation, rearm and tick-wrap tests passed");
    return 0;
}
