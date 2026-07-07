/*
 * rtc_time_bl602.c - Bouffalo (BL602/BL616/BL616CL) port of the shared RTC seam (rtc_time.h):
 * a retained wall clock across a warm reboot / deep-sleep wake, mirroring what AmebaD's rtc_time.c
 * does over the Realtek RTC.
 *
 * Bouffalo's hardware RTC is a free-running 32768 Hz counter in the HBN always-on domain (bflb_rtc);
 * board_init() does not reset it, and the HBN domain survives a soft reset / deep-sleep wake (only a
 * cold power-on zeroes it). The counter holds elapsed time, not the wall clock - so on every sync we
 * persist a reference pair {epoch, counter-seconds} to the KV store, and at boot reconstruct
 *   now = epoch0 + (counter_now - counter0).
 * If the counter reset (cold boot, counter_now < counter0) we fall back to the last synced epoch
 * (stale but plausible; SNTP re-syncs within seconds and corrects it).
 *
 * Logs via the shared rxi logger (at_log.h) like every other Bouffalo module - bflb_log is gone.
 */
#include <stdio.h>          /* snprintf / sscanf */
#include <stdlib.h>         /* setenv / getenv */
#include <time.h>

#include "rtc_time.h"
#include "syscalls.h"       /* amebad_set_unix_time (time_base.c) */
#include "bflb_core.h"      /* bflb_device_get_by_name / struct bflb_device_s */
#include "bflb_rtc.h"       /* bflb_rtc_get_time + BFLB_RTC_TIME2SEC */
#include "at_log.h"         /* rxi log: log_info (the single project logger) */

/* syscall_config: persist one env key to /KV (getenv()==NULL -> erase). */
extern int env_save( const char * name );

/* Below this the reference clearly holds no real time (2020-09-13). */
#define RTC_TIME_VALID_MIN  ( ( time_t ) 1600000000 )

/* KV key holding "<epoch>:<counter_seconds>" from the last sync. */
#define RTC_REF_KEY  "RTC_REF"

static struct bflb_device_s * rtc_dev( void )
{
    static struct bflb_device_s * dev;
    if( dev == NULL )
    {
        dev = bflb_device_get_by_name( "rtc" );
    }
    return dev;
}

/* Current RTC free-running counter, in seconds. */
static unsigned long rtc_counter_secs( struct bflb_device_s * dev )
{
    return ( unsigned long ) BFLB_RTC_TIME2SEC( bflb_rtc_get_time( dev ) );
}

/* Store a UTC timestamp + the current RTC counter so a later boot can reconstruct the wall clock. */
void rtc_time_store( time_t secs )
{
    struct bflb_device_s * dev = rtc_dev();
    char buf[ 40 ];

    if( dev == NULL )
    {
        return;
    }
    /* %lu, not %lld: bl602's newlib snprintf doesn't handle the 64-bit length modifier. The epoch fits
     * in 32 bits until 2038, and the RTC counter-seconds likewise. */
    snprintf( buf, sizeof( buf ), "%lu:%lu", ( unsigned long ) secs, rtc_counter_secs( dev ) );
    if( setenv( RTC_REF_KEY, buf, 1 ) == 0 )
    {
        env_save( RTC_REF_KEY );
    }
}

/* Seed the RAM wall clock from the persisted reference + the RTC counter. Returns 1 if a valid time
 * was restored, else 0. */
int rtc_time_seed( void )
{
    struct bflb_device_s * dev = rtc_dev();
    const char *           ref = getenv( RTC_REF_KEY );
    unsigned long          epoch0 = 0;
    unsigned long          cnt0 = 0;
    unsigned long          cnt_now;
    int                    retained;
    time_t                 now;

    if( dev == NULL || ref == NULL || sscanf( ref, "%lu:%lu", &epoch0, &cnt0 ) != 2 )
    {
        return 0;
    }

    cnt_now  = rtc_counter_secs( dev );
    retained = ( cnt_now >= cnt0 );                        /* counter kept running across the reset */
    now      = retained ? ( time_t ) epoch0 + ( time_t ) ( cnt_now - cnt0 )
                        : ( time_t ) epoch0;               /* counter reset (cold boot): last epoch */

    if( now >= RTC_TIME_VALID_MIN )
    {
        amebad_set_unix_time( now, 0 );
        log_info( "[RTC] seeded wall clock: unix=%lu (%s)", ( unsigned long ) now,
                  retained ? "counter-retained" : "stale/last-sync" );
        return 1;
    }

    log_info( "[RTC] no valid RTC reference (unix=%lu); waiting for SNTP", ( unsigned long ) now );
    return 0;
}
