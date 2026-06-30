/*
 * time_selftest.c — exercises newlib's date/time API after an SNTP sync and prints every
 * result (and any gap) under a [TIME] tag. End-to-end check of the time_base.c retarget
 * (_gettimeofday / _times / settimeofday / clock_gettime) plus the libc functions that ride
 * on them (time/gettimeofday/clock/gmtime/localtime/mktime/strftime/ctime/difftime/tzset).
 *
 * Called once, from the SNTP first-sync notification (sntp_time.c), so the clock reads real UTC.
 */
#include <stdio.h>
#include <stdlib.h>          /* setenv */
#include <string.h>
#include <time.h>            /* time/gmtime_r/localtime_r/mktime/strftime/ctime/tzset + _timezone... */
#include <sys/time.h>        /* gettimeofday, struct timeval, struct timespec */
#include "at_log.h"             /* rxi log.c: log_info / log_error */

/* <time.h> gates clock_gettime behind _POSIX_TIMERS (off in this newlib); time_base.c defines it,
 * so declare it ourselves to call it. CLOCK_REALTIME is unguarded in <time.h>. */
extern int clock_gettime( clockid_t clock_id, struct timespec * tp );
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME ( ( clockid_t ) 1 )
#endif

static void test_tz( const char * tz )
{
    time_t t = time( NULL );
    struct tm lt;
    char buf[ 64 ];

    setenv( "TZ", tz, 1 );        /* env.c backs getenv("TZ") that tzset() reads */
    tzset();
    localtime_r( &t, &lt );
    strftime( buf, sizeof( buf ), "%Y-%m-%d %H:%M:%S %Z", &lt );
    log_info( "[TIME] TZ=%-8s local=%s  (_timezone=%ld _daylight=%d tzname=%s/%s)",
            tz, buf, _timezone, _daylight, _tzname[ 0 ], _tzname[ 1 ] );
}

/* Build a UTC instant from a calendar date: mktime() under TZ=UTC0 treats the struct tm as UTC,
 * so the result is the true epoch — a portable timegm() without depending on the live clock. */
static time_t utc_epoch( int year, int mon, int day, int hour )
{
    struct tm tm;

    memset( &tm, 0, sizeof( tm ) );
    tm.tm_year = year - 1900;
    tm.tm_mon  = mon - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    setenv( "TZ", "UTC0", 1 );
    tzset();
    return mktime( &tm );
}

/* One DST probe: print the UTC ground-truth + the local rendering under `tz`, and flag if newlib's
 * tm_isdst disagrees with what's expected (0 = standard/winter time, 1 = DST/summer time). */
static void check_dst( const char * tz, time_t t, int expect_isdst )
{
    struct tm lt, ut;
    char lb[ 40 ], ub[ 24 ];

    setenv( "TZ", tz, 1 );
    tzset();
    localtime_r( &t, &lt );
    gmtime_r( &t, &ut );
    strftime( ub, sizeof( ub ), "%m-%d %H:%M", &ut );
    strftime( lb, sizeof( lb ), "%m-%d %H:%M %Z", &lt );
    log_info( "[DST] %-26s UTC %s -> local %s isdst=%d %s",
            tz, ub, lb, lt.tm_isdst,
            ( lt.tm_isdst == expect_isdst ) ? "ok" : "*** isdst MISMATCH ***" );
}

/* Does this newlib correctly drop to standard (winter) time outside the DST window, and apply the
 * DST rules at the right boundaries? Checks deep winter/summer + both 2026 US transitions, under
 * implicit-US (PST8PDT), explicit-EU (CET-1CEST,...) and no-DST (CST-8) zones. Pure libc — no clock. */
static void dst_selftest( void )
{
    time_t jan = utc_epoch( 2026, 1, 15, 12 );    /* deep winter */
    time_t jul = utc_epoch( 2026, 7, 15, 12 );    /* deep summer */

    log_info( "[DST] ==== newlib DST / standard-time (winter) test ====" );

    /* US Pacific, implicit POSIX (US) rules: winter -> PST (std), summer -> PDT. */
    check_dst( "PST8PDT", jan, 0 );
    check_dst( "PST8PDT", jul, 1 );

    /* EU Central, explicit rules (last-Sun-Mar .. last-Sun-Oct): winter -> CET, summer -> CEST. */
    check_dst( "CET-1CEST,M3.5.0,M10.5.0/3", jan, 0 );
    check_dst( "CET-1CEST,M3.5.0,M10.5.0/3", jul, 1 );

    /* China: no DST -> standard all year. */
    check_dst( "CST-8", jan, 0 );
    check_dst( "CST-8", jul, 0 );

    /* US fall-back into winter time: 2026-11-01, 02:00 PDT -> 01:00 PST (switch @ 09:00 UTC). */
    check_dst( "PST8PDT", utc_epoch( 2026, 11, 1, 8  ), 1 );   /* 01:00 PDT, before */
    check_dst( "PST8PDT", utc_epoch( 2026, 11, 1, 10 ), 0 );   /* 02:00 PST, after  */

    /* US spring-forward out of winter time: 2026-03-08, 02:00 PST -> 03:00 PDT (switch @ 10:00 UTC). */
    check_dst( "PST8PDT", utc_epoch( 2026, 3, 8, 9  ), 0 );    /* 01:00 PST, before */
    check_dst( "PST8PDT", utc_epoch( 2026, 3, 8, 11 ), 1 );    /* 03:00 PDT, after  */

    log_info( "[DST] ==== DST test done ====" );
}

void time_selftest_run( void )
{
    time_t t;
    struct timeval tv;
    struct tm utc;
    struct timespec ts;
    char buf[ 64 ];
    int r;
    char saved_tz[ 16 ] = { 0 };
    const char * tz0 = getenv( "TZ" );   /* boot default (main() sets CST-8) */

    if( tz0 != NULL )
    {
        strncpy( saved_tz, tz0, sizeof( saved_tz ) - 1 );
    }

    log_info( "[TIME] ==== newlib date/time self-test ====" );

    /* 0. stderr path: fprintf(stderr) must route fd 2 -> at_console_write's errputc -> LOGUART,
     *    format correctly, and pick up the per-line timestamp like stdout. (fprintf is NOT caught
     *    by platform_stdlib's printf->_rtl_printf macro, so it uses the real newlib stdio path.) */
    fprintf( stderr, "[STDERR] fprintf(stderr) ok: fileno=%d int=%d str=%s hex=0x%x\n\r",
             fileno( stderr ), 42, "hi", 0xBEEF );
    fflush( stderr );

    /* 1. time() / gettimeofday() -> _gettimeofday (time_base.c) */
    t = time( NULL );
    gettimeofday( &tv, NULL );
    log_info( "[TIME] time()=%ld  gettimeofday=%ld.%06ld",
            ( long ) t, ( long ) tv.tv_sec, ( long ) tv.tv_usec );

    /* 2. gmtime_r + strftime (UTC; pure libc over time_t) */
    gmtime_r( &t, &utc );
    strftime( buf, sizeof( buf ), "%Y-%m-%d %H:%M:%S UTC", &utc );
    log_info( "[TIME] gmtime/strftime = %s", buf );

    /* 3. localtime/tzset over the TZ env var */
    test_tz( "CST-8" );          /* China = UTC+8, expect "CST" */
    test_tz( "PST8PDT" );        /* US Pacific, default DST rules */
    test_tz( "UTC0" );           /* back to UTC for the mktime round-trip below */

    /* 4. mktime round-trip (treats struct tm as LOCAL; TZ=UTC0 now -> back == t) */
    {
        struct tm m = utc;
        time_t back = mktime( &m );
        log_info( "[TIME] mktime(gmtime(t))=%ld  (delta vs t = %ld)",
                ( long ) back, ( long ) ( back - t ) );
    }

    /* 5. ctime + difftime (pure libc; difftime printed as int to avoid float-printf dep) */
    log_info( "[TIME] ctime = %s", ctime( &t ) );        /* ctime() already appends '\n' */
    log_info( "[TIME] difftime(t+3600,t) = %ld (expect 3600)", ( long ) difftime( t + 3600, t ) );

    /* 6. clock() over _times; CLOCKS_PER_SEC=100 on this ARM newlib -> seconds = clock()/CLOCKS_PER_SEC */
    {
        clock_t c = clock();
        log_info( "[TIME] clock()=%ld  (CLOCKS_PER_SEC=%ld -> %ld s uptime)",
                ( long ) c, ( long ) CLOCKS_PER_SEC, ( long ) ( c / CLOCKS_PER_SEC ) );
    }

    /* 7. clock_gettime(CLOCK_REALTIME) — supplemented in time_base.c */
    r = clock_gettime( CLOCK_REALTIME, &ts );
    log_info( "[TIME] clock_gettime(REALTIME) r=%d  %ld.%09ld",
            r, ( long ) ts.tv_sec, ts.tv_nsec );

    /* 8. DST / standard-time (winter) handling — independent of the live clock. */
    dst_selftest();

    /* The TZ probes above left TZ=UTC0; restore the boot default (CST-8) so normal
     * logs read local time again. */
    if( saved_tz[ 0 ] != '\0' )
    {
        setenv( "TZ", saved_tz, 1 );
    }
    else
    {
        unsetenv( "TZ" );
    }
    tzset();

    log_info( "[TIME] ==== self-test done ====" );
}
