/*
 * time_base.c — newlib wall-clock retarget for the cross-platform syscall layer.
 *
 * Supplies the four time symbols libc.a leaves undefined/absent, so newlib's whole
 * date/time API works over a real UTC base:
 *   - _gettimeofday  — libc time()/gettimeofday() reference it (U in libc.a); strong def here.
 *   - _times         — libc times()/clock() reference it (U in libc.a); strong def here.
 *   - settimeofday   — not in libc.a at all; full def here.
 *   - clock_gettime  — not in libc.a; <time.h> ALSO hides its prototype behind _POSIX_TIMERS
 *                      (off in this newlib — it doesn't even pull sys/features.h before the
 *                      check), so we declare it ourselves and define it.
 * libc provides the rest over these: time/gettimeofday/times/clock/gmtime/localtime/mktime/
 * strftime/ctime/difftime/tzset.
 *
 * The base is a RAM { Unix seconds, usec, tick } triple set by amebad_set_unix_time() (call it
 * from an SNTP/RTC source). Elapsed time since the base comes from the FreeRTOS tick = SysTick
 * @ configTICK_RATE_HZ (1 ms). A seqlock makes the triple read atomically — a torn read mixing
 * a fresh base with a stale tick would jump the clock by seconds. Before the first set, time()
 * reports 1970 + uptime (intentional, harmless pre-sync behavior).
 */
#include <sys/time.h>      /* struct timeval, struct timezone, settimeofday/_gettimeofday protos */
#include <sys/times.h>     /* struct tms, _times proto */
#include <time.h>          /* struct timespec, clockid_t, CLOCK_REALTIME */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>         /* snprintf (at_console_timestamp) */

#include "FreeRTOS.h"
#include "task.h"
#include "syscalls.h"      /* amebad_set_unix_time() */
#include "console.h"       /* at_console_timestamp() prototype (weak in console.c) */

/* <time.h> gates clock_gettime + CLOCK_MONOTONIC behind _POSIX_TIMERS / _POSIX_MONOTONIC_CLOCK,
 * undefined in this newlib config; CLOCK_REALTIME is unguarded. Provide what's missing so our
 * strong def keeps the standard ABI (and callers can prototype it the same way). */
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC ( ( clockid_t ) 4 )
#endif
int clock_gettime( clockid_t clock_id, struct timespec * tp );

/* ---- RAM time base (seqlock; single writer = the SNTP setTime callback) ----------------- */
static volatile uint32_t s_seq;        /* even = stable, odd = write in progress */
static volatile uint32_t s_base_unix;  /* Unix seconds at last set */
static volatile uint32_t s_base_usec;  /* sub-second usec at last set */
static volatile uint32_t s_base_tick;  /* xTaskGetTickCount() at last set */
static volatile int      s_synced;     /* 0 until the first amebad_set_unix_time() */

void amebad_set_unix_time( time_t secs, unsigned int usec )
{
    s_seq++;                                       /* -> odd: readers in-progress will retry */
    __asm volatile ( "" ::: "memory" );
    s_base_unix = ( uint32_t ) secs;
    s_base_usec = usec % 1000000u;
    s_base_tick = ( uint32_t ) xTaskGetTickCount();
    s_synced    = 1;
    __asm volatile ( "" ::: "memory" );
    s_seq++;                                       /* -> even: stable */
}

/* Read the base atomically and project to "now". Returns s_synced (0 before the first set). */
static int now_unix( uint32_t * out_sec, uint32_t * out_usec )
{
    uint32_t s0, s1, b_unix, b_usec, b_tick;
    int synced;

    do
    {
        s0 = s_seq;
        __asm volatile ( "" ::: "memory" );
        b_unix = s_base_unix;
        b_usec = s_base_usec;
        b_tick = s_base_tick;
        synced = s_synced;
        __asm volatile ( "" ::: "memory" );
        s1 = s_seq;
    } while( ( s0 & 1u ) || ( s0 != s1 ) );

    /* Unsigned tick subtraction is wrap-safe; re-sync interval (~1 h) << 49.7-day wrap. */
    uint32_t elapsed_ms = ( ( uint32_t ) xTaskGetTickCount() - b_tick ) * ( uint32_t ) portTICK_PERIOD_MS;
    uint32_t usec       = b_usec + ( elapsed_ms % 1000u ) * 1000u;

    *out_sec  = b_unix + elapsed_ms / 1000u + usec / 1000000u;
    *out_usec = usec % 1000000u;
    return synced;
}

/* ---- per-line log timestamp (strong override of the weak stub in console.c) ------------- */

/* at_console_write() prepends this to every log line. Once the clock is SNTP-synced it reads
 * "[HH:MM:SS.mmm] " in local time via localtime_r (honors TZ + DST per the active rules; UTC
 * until `TZ` is set). Before the first sync there is no real wall clock, so it falls back to
 * "[+<sec>.<mmm>] " uptime from the FreeRTOS tick. */
int at_console_timestamp( char * buf, int cap )
{
    uint32_t sec, usec;

    if( now_unix( &sec, &usec ) )                       /* synced -> wall clock */
    {
        time_t    t = ( time_t ) sec;
        struct tm lt;

        localtime_r( &t, &lt );                         /* applies TZ offset + DST */
        return snprintf( buf, ( size_t ) cap, "[%02u:%02u:%02u.%03u] ",
                         ( unsigned ) lt.tm_hour, ( unsigned ) lt.tm_min,
                         ( unsigned ) lt.tm_sec, ( unsigned ) ( usec / 1000u ) );
    }
    else                                                /* pre-sync -> uptime since boot */
    {
        unsigned long ms = ( unsigned long ) xTaskGetTickCount() * ( unsigned long ) portTICK_PERIOD_MS;

        return snprintf( buf, ( size_t ) cap, "[+%lu.%03lu] ", ms / 1000uL, ms % 1000uL );
    }
}

/* ---- newlib time syscalls (strong defs win from the OBJECT lib) ------------------------- */

int _gettimeofday( struct timeval * tv, void * tz )
{
    ( void ) tz;
    if( tv != NULL )
    {
        uint32_t s, us;
        ( void ) now_unix( &s, &us );
        tv->tv_sec  = ( time_t ) s;
        tv->tv_usec = ( suseconds_t ) us;
    }
    return 0;
}

clock_t _times( struct tms * buf )
{
    /* clock_t counts in CLOCKS_PER_SEC units. On this newlib CLOCKS_PER_SEC == 100 (machine/time.h,
     * ARM) while the FreeRTOS tick is 1 ms (configTICK_RATE_HZ == 1000), so scale ticks -> clocks by
     * CLOCKS_PER_SEC/tick-rate (= /10). clock() then yields seconds via clock()/CLOCKS_PER_SEC. */
    clock_t t = ( clock_t ) ( xTaskGetTickCount() / ( configTICK_RATE_HZ / CLOCKS_PER_SEC ) );

    if( buf != NULL )
    {
        buf->tms_utime  = t;
        buf->tms_stime  = 0;
        buf->tms_cutime = 0;
        buf->tms_cstime = 0;
    }
    return t;
}

int settimeofday( const struct timeval * tv, const struct timezone * tz )
{
    ( void ) tz;
    if( tv == NULL )
    {
        errno = EINVAL;
        return -1;
    }
    amebad_set_unix_time( tv->tv_sec, ( unsigned int ) tv->tv_usec );
    return 0;
}

int clock_gettime( clockid_t clock_id, struct timespec * tp )
{
    if( tp == NULL )
    {
        errno = EINVAL;
        return -1;
    }
    if( clock_id == CLOCK_REALTIME )
    {
        uint32_t s, us;
        ( void ) now_unix( &s, &us );
        tp->tv_sec  = ( time_t ) s;
        tp->tv_nsec = ( long ) us * 1000L;
        return 0;
    }
    if( clock_id == CLOCK_MONOTONIC )
    {
        uint32_t ms = ( uint32_t ) xTaskGetTickCount() * ( uint32_t ) portTICK_PERIOD_MS;
        tp->tv_sec  = ( time_t ) ( ms / 1000u );
        tp->tv_nsec = ( long ) ( ms % 1000u ) * 1000000L;
        return 0;
    }
    errno = EINVAL;
    return -1;
}
