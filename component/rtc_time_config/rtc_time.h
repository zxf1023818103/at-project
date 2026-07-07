/*
 * rtc_time.h - shared seam for a retained wall clock across a reboot / deep-sleep wake.
 *
 * sntp_time.c mirrors each freshly-synced UTC into the platform's retained hardware RTC via
 * rtc_time_store(); at boot the platform seeds the newlib wall clock from that RTC via rtc_time_seed()
 * so time() reads real local time before SNTP has had a chance to re-sync.
 *
 * Both are platform-specific (different RTC hardware), so each platform provides its own strong
 * implementation of this ONE interface:
 *   - AmebaD (RTL8721D): project/amebad/app_km4/rtc_time.c, over the mbed rtc_api + RRAM year.
 *   - Bouffalo (BL602/616/616CL): component/bouffalo_sdk/rtc_time_port, over the bflb_rtc HBN-domain
 *     counter + a KV-persisted reference.
 * A platform with no HW RTC simply links neither: sntp_time.c calls rtc_time_store through a weak
 * reference (skipped when unresolved), and each main() calls rtc_time_seed() only where a port exists.
 */
#ifndef RTC_TIME_H
#define RTC_TIME_H

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mirror a freshly-synced UTC timestamp into the retained hardware RTC. Called by sntp_time.c on
 * every sync (through a weak reference, so a port-less platform links nothing). */
void rtc_time_store( time_t secs );

/* Seed the newlib wall clock from the retained hardware RTC if it holds a plausible date. Returns 1
 * if a valid time was restored, else 0. Called from each platform's boot bring-up. */
int  rtc_time_seed( void );

#ifdef __cplusplus
}
#endif

#endif /* RTC_TIME_H */
