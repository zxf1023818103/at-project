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
    printf( "[TIME] TZ=%-8s local=%s  (_timezone=%ld _daylight=%d tzname=%s/%s)\n\r",
            tz, buf, _timezone, _daylight, _tzname[ 0 ], _tzname[ 1 ] );
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

    printf( "[TIME] ==== newlib date/time self-test ====\n\r" );

    /* 1. time() / gettimeofday() -> _gettimeofday (time_base.c) */
    t = time( NULL );
    gettimeofday( &tv, NULL );
    printf( "[TIME] time()=%ld  gettimeofday=%ld.%06ld\n\r",
            ( long ) t, ( long ) tv.tv_sec, ( long ) tv.tv_usec );

    /* 2. gmtime_r + strftime (UTC; pure libc over time_t) */
    gmtime_r( &t, &utc );
    strftime( buf, sizeof( buf ), "%Y-%m-%d %H:%M:%S UTC", &utc );
    printf( "[TIME] gmtime/strftime = %s\n\r", buf );

    /* 3. localtime/tzset over the TZ env var */
    test_tz( "CST-8" );          /* China = UTC+8, expect "CST" */
    test_tz( "PST8PDT" );        /* US Pacific, default DST rules */
    test_tz( "UTC0" );           /* back to UTC for the mktime round-trip below */

    /* 4. mktime round-trip (treats struct tm as LOCAL; TZ=UTC0 now -> back == t) */
    {
        struct tm m = utc;
        time_t back = mktime( &m );
        printf( "[TIME] mktime(gmtime(t))=%ld  (delta vs t = %ld)\n\r",
                ( long ) back, ( long ) ( back - t ) );
    }

    /* 5. ctime + difftime (pure libc; difftime printed as int to avoid float-printf dep) */
    printf( "[TIME] ctime = %s", ctime( &t ) );        /* ctime() already appends '\n' */
    printf( "[TIME] difftime(t+3600,t) = %ld (expect 3600)\n\r", ( long ) difftime( t + 3600, t ) );

    /* 6. clock() over _times; CLOCKS_PER_SEC=100 on this ARM newlib -> seconds = clock()/CLOCKS_PER_SEC */
    {
        clock_t c = clock();
        printf( "[TIME] clock()=%ld  (CLOCKS_PER_SEC=%ld -> %ld s uptime)\n\r",
                ( long ) c, ( long ) CLOCKS_PER_SEC, ( long ) ( c / CLOCKS_PER_SEC ) );
    }

    /* 7. clock_gettime(CLOCK_REALTIME) — supplemented in time_base.c */
    r = clock_gettime( CLOCK_REALTIME, &ts );
    printf( "[TIME] clock_gettime(REALTIME) r=%d  %ld.%09ld\n\r",
            r, ( long ) ts.tv_sec, ts.tv_nsec );

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

    printf( "[TIME] ==== self-test done ====\n\r" );
}
