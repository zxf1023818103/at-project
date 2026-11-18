/* Retained hardware RTC <-> shared newlib wall clock, after TZ/env loading. */
#include "rtc_api.h"
#include "syscalls.h"
#include <stdio.h>
#include <time.h>

void rtc_time_store(time_t seconds)
{
    if (seconds < (time_t)1600000000) return;
    if (!rtc_isenabled()) rtc_init();
    rtc_write(seconds);
}

int rtc_time_seed(void)
{
    rtc_init();
    time_t seconds = rtc_read();
    if (seconds < (time_t)1600000000) return 0;
    amebad_set_unix_time(seconds, 0);
    printf("[RTC] seeded wall clock: unix=%lld\n", (long long)seconds);
    return 1;
}

void app_platform_rtc_seed(void) { (void)rtc_time_seed(); }
