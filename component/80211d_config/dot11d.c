/*
 * dot11d.c — the shared half of the 802.11d country auto-detect: gate + tally/vote + persistence.
 * The driver-specific scan/apply is behind the dot11d_port_* contract (see dot11d.h).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dot11d.h"

/* Weak default: no extra platform gate. AmebaD's port strong-overrides (WIFI_CHANPLAN). */
__attribute__( ( weak ) ) int dot11d_port_provisioned( void )
{
    return 0;
}

void dot11d_autodetect_country( void )
{
    dot11d_tally_t t;
    char           ccs[ 3 ];

    if( dot11d_cc_valid( getenv( "WIFI_COUNTRY" ) ) || dot11d_port_provisioned() )
    {
        return;   /* regulatory domain already provisioned - leave it alone (garbage != provisioned) */
    }

    memset( &t, 0, sizeof( t ) );
    if( dot11d_port_scan( &t ) != 0 )
    {
        printf( "[80211d] country auto-detect: scan failed - keeping default plan\r\n" );
        return;
    }
    if( dot11d_tally_best( &t, ccs ) == NULL )
    {
        printf( "[80211d] country auto-detect: %d APs scanned, none advertised a Country IE - keeping default plan\r\n",
                t.aps );
        return;
    }
    printf( "[80211d] country auto-detect: %d APs, majority %s -> set + save\r\n", t.aps, ccs );
    setenv( "WIFI_COUNTRY", ccs, 1 );   /* persists via the setenv wrap (KV/LFS) */
    dot11d_port_apply( ccs );
}
