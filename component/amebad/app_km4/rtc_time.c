/*
 * rtc_time.c — bridge the AmebaD hardware RTC <-> the newlib wall clock (time_base.c).
 *
 *   - on sync  : push fresh UTC into the RTC (sntp_time.c calls rtc_time_store on every sync);
 *   - on boot  : seed the RAM wall clock from the RTC (if it holds a plausible date), so time()
 *                reads real time before SNTP has had a chance to sync.
 *
 * The mbed rtc_api stores via localtime() and reads via mktime(), consistent under a fixed TZ
 * (main() defaults TZ=CST-8). The RTC *hardware* keeps day-of-year + H:M:S, but the calendar
 * YEAR is held in retention RAM (RRAM->RTC_YEAR, via rtc_backup_timeinfo()).
 *
 * Retention caveat (measured on RTL8721D): the RTC hardware calendar survives a warm reset, but
 * RRAM is cleared by a WDG/CHIP_EN reset — so after a normal `reboot` the year is lost and the
 * seed reads a 1900-based (negative) time, which we reject (< RTC_TIME_VALID_MIN) and fall back
 * to SNTP. The seed therefore restores time across a deep-sleep wake (RRAM retained), not across
 * a full reboot. Surviving a reboot would need the epoch persisted to flash (LittleFS) — TODO if
 * wanted. A cold power-on also leaves the RTC at ~1970, likewise handled by the reject path.
 */
#include <stdio.h>
#include <time.h>

#include "rtc_api.h"      /* rtc_init / rtc_isenabled / rtc_read / rtc_write (mbed HAL) */
#include "syscalls.h"     /* amebad_set_unix_time (time_base.c) */
#include "at_log.h"          /* rxi log.c: log_info / log_error */

/* Declared in rtc_api.c but not in rtc_api.h: persist tm_year + valid bit to RRAM. */
extern void rtc_backup_timeinfo( void );

/* Below this the RTC clearly holds no real time (2020-09-13). A cold boot reads ~1970. */
#define RTC_TIME_VALID_MIN  ( ( time_t ) 1600000000 )

/* Store a UTC timestamp into the RTC and persist the year to retention RAM. */
void rtc_time_store( time_t secs )
{
    if( !rtc_isenabled() )
    {
        rtc_init();
    }
    rtc_write( secs );
    rtc_backup_timeinfo();
}

/* Seed the RAM wall clock from the RTC. Returns 1 if a valid time was restored, else 0. */
int rtc_time_seed( void )
{
    time_t t;

    rtc_init();
    t = rtc_read();

    if( t >= RTC_TIME_VALID_MIN )
    {
        amebad_set_unix_time( t, 0 );
        log_info( "[RTC] seeded wall clock from RTC: unix=%lld", ( long long ) t );
        return 1;
    }

    log_info( "[RTC] no valid time in RTC (unix=%lld); waiting for SNTP", ( long long ) t );
    return 0;
}
