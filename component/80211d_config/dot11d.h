/*
 * dot11d.h — cross-platform 802.11d country-code auto-detect (component 80211d).
 *
 * Model: when no valid WIFI_COUNTRY is provisioned, run ONE scan, tally every surrounding AP's
 * beacon 802.11d Country IE, take the majority, persist it to WIFI_COUNTRY (KV/LFS via the setenv
 * wrap) and apply it to the WiFi driver. Config layer (this component) owns the gate, the tally,
 * the vote and the persistence; everything driver-specific — how a scan runs and how the per-AP
 * Country IE is read from its records, plus how a code is applied — lives in each platform's
 * 80211d_port:
 *   amebad   : extended-country scan records (lib_wlan) + wifi_set_channel_plan_by_country_code
 *              (driver reboot; WIFI_CHANPLAN provisioning also counts as "provisioned")
 *   bouffalo : wifi_mgmr scan items (Country IE threaded through the at-project wifi4 patch) +
 *              wifi_mgmr_set_country_code (runtime, no reboot). bl602/BL702 only — the wl80211
 *              chips (bl616/cl) don't carry the IE in their scan records yet.
 */
#ifndef DOT11D_H
#define DOT11D_H

#ifdef __cplusplus
extern "C" {
#endif

/* Tally of the per-AP Country IEs seen in one scan (the majority vote). Zero-init, then
 * dot11d_tally_add() once per AP heard (invalid/absent codes count in .aps only). */
typedef struct
{
    char cc[ 16 ][ 2 ];
    int  count[ 16 ];
    int  num;
    int  aps;
} dot11d_tally_t;

void         dot11d_tally_add( dot11d_tally_t * t, char c0, char c1 );
const char * dot11d_tally_best( dot11d_tally_t * t, char out[ 3 ] );   /* NULL if no votes */

/* 1 iff cc looks like a 2-letter uppercase country code ("CN"). Use on the WIFI_COUNTRY env before
 * trusting it (a garbage/empty persisted value must neither reach the driver nor block detect). */
int dot11d_cc_valid( const char * cc );

/* Run the auto-detect once (gate -> port scan -> vote -> setenv WIFI_COUNTRY -> port apply).
 * Call from the platform's WiFi bring-up, after the driver is up and BEFORE the first associate.
 * No-op when already provisioned or when the scan yields no Country IEs. */
void dot11d_autodetect_country( void );

/* ===== Port contract — implemented by each platform's 80211d_port ========================== */

/* One scan; feed dot11d_tally_add() for every AP heard. Returns 0 when the scan ran. */
int dot11d_port_scan( dot11d_tally_t * t );

/* Apply the winning code to the WiFi driver. */
void dot11d_port_apply( const char cc[ 3 ] );

/* Extra platform "already provisioned" gate beyond WIFI_COUNTRY (weak default: 0). AmebaD
 * returns 1 when WIFI_CHANPLAN is set (a raw channel-plan overrides country detection). */
int dot11d_port_provisioned( void );

#ifdef __cplusplus
}
#endif

#endif /* DOT11D_H */
