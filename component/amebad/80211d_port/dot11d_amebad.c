/*
 * dot11d_amebad.c — the AmebaD (RTL8721D KM4) port of the 802.11d country auto-detect
 * (component/80211d_config, dot11d.h).
 *
 * scan : the driver's extended-country scan mode (lib_wlan.a) appends each AP's 2-byte beacon
 *        Country IE into the scan-result records at offset 14; one async
 *        wifi_scan_networks_with_ssid pass tallies every AP heard. (NOT the SDK's sync wifi_scan /
 *        wifi_scan_networks_with_extended_countryinfo, whose scan-done never returns in this
 *        driver-only image.)
 * apply: wifi_set_channel_plan_by_country_code — applies the plan now and triggers a driver
 *        reboot; on the reboot WIFI_COUNTRY is set (persisted by the config layer), so
 *        wifi_set_country_code() applies the same plan during wifi_on and detection is skipped.
 * gate : WIFI_CHANPLAN (a raw provisioned channel plan) also counts as "provisioned" — strong
 *        override of the config layer's weak dot11d_port_provisioned.
 *
 * Call dot11d_autodetect_country() after wifi_on, before associating.
 */
#include <stdlib.h>
#include <string.h>

#include "wifi_conf.h"   /* wifi_scan_networks_with_ssid / wifi_set_channel_plan_by_country_code */
#include "dot11d.h"

/* Extended-country scan mode (lib_wlan.a). */
extern void rltk_wlan_enable_scan_with_extended_countryinfo( int enable );

/* Per-AP walk over the extended-country scan records. Record layout:
 * [0]=len [1..6]=bssid [7..10]=rssi [11]=sec [12]=wps [13]=channel [14..15]=country [16..]=ssid. */
static int prv_country_scan_handler( char * buf, int buflen, char * ssid, void * user_data )
{
    dot11d_tally_t * t = ( dot11d_tally_t * ) user_data;
    int plen = 0;

    ( void ) ssid;
    while( plen < buflen )
    {
        int len = ( unsigned char ) buf[ plen ];
        if( len == 0 )
        {
            break;
        }
        dot11d_tally_add( t, buf[ plen + 14 ], buf[ plen + 15 ] );
        plen += len;
    }
    return 0;
}

int dot11d_port_scan( dot11d_tally_t * t )
{
    /* May be NULL -> broadcast probe; the scan buffer carries every AP heard regardless of the
     * probed SSID, so one scan tallies all surrounding Country IEs. */
    const char * ssid = getenv( "WIFI_SSID" );

    rltk_wlan_enable_scan_with_extended_countryinfo( 1 );
    wifi_scan_networks_with_ssid( prv_country_scan_handler, t, 1000,
                                  ( char * ) ssid, ssid ? ( int ) strlen( ssid ) : 0 );
    rltk_wlan_enable_scan_with_extended_countryinfo( 0 );
    return 0;
}

void dot11d_port_apply( const char cc[ 3 ] )
{
    wifi_set_channel_plan_by_country_code( ( unsigned char * ) cc );   /* applies now; driver reboot */
}

int dot11d_port_provisioned( void )
{
    return getenv( "WIFI_CHANPLAN" ) != NULL;   /* a raw channel plan overrides country detection */
}
