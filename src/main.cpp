// ===================================================================================
// AmebaD (RTL8721D / RTL8710C) KM4 app entry — folded from project/amebad/app_km4/main.c.
// AmebaD compiles ONLY this file as its image2 entry (project/amebad/app_km4 add_executable),
// so its Realtek-driver `main()` lives here under the platform guard. It is C code compiled as
// C++ with C linkage (the whole body is wrapped in `extern "C"` further down) so the FreeRTOS+TCP
// hooks (vApplicationIPNetworkEventHook etc.) keep their C symbol names. The bl602/ameba-newer
// worlds are below, untouched.
// ===================================================================================
#if defined(CONFIG_PLATFORM_8721D) || defined(CONFIG_PLATFORM_8710C)
#include <stdio.h>                /* printf (timestamped [KM4] logs, replacing DiagPrintf) */
#include <stdlib.h>               /* setenv (default TZ) */
#include <time.h>                 /* tzset */
/* The Realtek SDK + FreeRTOS headers are C; wrap them in extern "C" so every C-function
 * declaration they make (incl. FreeRTOSConfig.h's freertos_*_sleep_processing) gets C linkage
 * when this file is compiled as C++, matching their C definitions (no linkage conflicts). */
extern "C" {
#include "ameba_soc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "wifi_conf.h"            /* wifi_on / wifi_connect / wifi_get_mac_address */
#include "wifi_ind.h"             /* wifi_ind EventGroup: wifi_event_wait / WIFI_EVENT_BIT */
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Routing.h"     /* NetworkInterface_t / NetworkEndPoint_t / FillEndPoint */
#ifdef AT_PROJECT_CARES
#include "cares_resolve.h"        /* c-ares resolver channel (CARES=ON) */
#endif
#include "FreeRTOS_DHCP.h"        /* eGetDHCPState / eNotUsingLeasedAddress */
#include "NetworkBufferManagement.h"  /* pxGetNetworkBufferWithDescriptor: MLDv2 report TX */
#include "FreeRTOS_ND.h"              /* vRAProcess: restart SLAAC after the MLD join */
#include "backoff_algorithm.h"    /* WiFi reconnect backoff-with-jitter */
#include "at_log.h"               /* rxi log.c: log_info / log_error */
#include "log_lock.h"             /* log_lock_init */
#include "wifi_service.h"         /* shared WiFi provisioning + reconnect policy */
#include "wifi_port.h"            /* the platform HAL it drives (implemented below) */
#include "app_startup.h"          /* shared startup helpers (app_env_time_init / app_console_start / app_on_network_up) */
}  // extern "C"
/* ameba_soc.h -> platform_stdlib does `#define printf _rtl_printf`; undo it so printf flows
 * through _write -> at_console_write and picks up the [HH:MM:SS] prefix. */
#undef printf
extern "C" {
/* console: the shared FreeRTOS-Plus-CLI engine (component/monitor_config) + the AmebaD
 * port (component/amebad/monitor_port: LOGUART RX + KM0/KM4 switch + reboot/dw/ew/flash/efuse) */
extern void monitor_start(void);
extern void amebad_monitor_cmds_register(void);

/* ROM/fwlib CPU clock select (rtl8721d_system.h; CLK_KM4_200M comes in via ameba_soc.h). */
extern void SystemSetCpuClk(unsigned char CpuClk);

/* FreeRTOS+TCP Realtek glue (component/amebad/freertos_tcp_port) */
extern NetworkInterface_t *pxRealtek_FillInterfaceDescriptor(BaseType_t xEMACIndex,
                                                             NetworkInterface_t *pxInterface);
extern void amebad_register_wlan_netif(int idx, unsigned char *mac);
extern void amebad_freertos_tcp_set_ready(void);
/* NTP server captured from DHCP option 42 (FreeRTOS_DHCP.c at-project patch); network byte order, 0=none. */
extern uint32_t ulDHCPNtpServerAddress;
/* Homegrown iperf2 throughput probes (make IPERF=ON); auto-run from the network-up hook. */
extern void iperf_client_start(void);
extern void iperf_server_start(void);
extern void iperf_udp_client_start(void);
extern void iperf_udp_server_start(void);

#define WLAN_STA_IDX  0

/* WiFi reconnect backoff: 1 s base, 8 s cap, RETRY_FOREVER (keep re-associating until the AP is back). */
#define WIFI_RECONNECT_BACKOFF_BASE_MS  ( 1000U )
#define WIFI_RECONNECT_BACKOFF_MAX_MS   ( 8000U )

static NetworkInterface_t xInterface;
/* NB: OpenOCD's FreeRTOS awareness needs the `uxTopUsedPriority` symbol - the kernel already exports
 * it (tasks.c), so we don't redefine it here. -rtos FreeRTOS is set in scripts/amebad_openocd.cfg. */
static NetworkEndPoint_t  xEndPoint;

/* WiFi link state lives in the wifi_ind EventGroup (WIFI_CONNECTED_BIT) rather than a standalone flag,
 * so the same group the driver-event indication feeds is the single source of truth. It is SET by the
 * eNetworkUp hook (the reliable "connected" cue - FOURWAY_HANDSHAKE_DONE is flaky on the driver's
 * autoreconnect, and polling wifi_is_connected_to_ap() stays "not connected" after a silent
 * autoreconnect) and CLEARED by the DISCONNECT event. It starts clear: the first real DHCP lease is then
 * a true transition, and the IPv6 endpoints coming up later don't re-fire "network UP". */
static int wifi_link_up( void )
{
    return ( wifi_event_get() & WIFI_CONNECTED_BIT ) != 0;
}
static void wifi_link_set( int up )
{
    if( up != 0 )
        ( void ) xEventGroupSetBits( wifi_event_group_handle(), WIFI_CONNECTED_BIT );
    else
        ( void ) wifi_event_clear( WIFI_CONNECTED_BIT );
}
/* "DISCONNECT seen, the monitor is re-associating" (WIFI_REASSOC_PENDING_BIT). While set, the eNetworkUp
 * hook drops every up: FreeRTOS_NetworkDown() re-evaluates the link off the briefly-stale
 * wifi_is_connected_to_ap() and fires a premature eNetworkUp with the OLD lease ~1 ms after a deauth -
 * honouring that would mask the outage and let the monitor skip the real re-associate. The monitor
 * clears it once wifi_connect() actually succeeds, so only the genuine post-re-associate DHCP up wins. */
static int wifi_reassoc_pending( void )
{
    return ( wifi_event_get() & WIFI_REASSOC_PENDING_BIT ) != 0;
}
static void wifi_reassoc_set( int pending )
{
    if( pending != 0 )
        ( void ) xEventGroupSetBits( wifi_event_group_handle(), WIFI_REASSOC_PENDING_BIT );
    else
        ( void ) wifi_event_clear( WIFI_REASSOC_PENDING_BIT );
}
/* wifi_forget() / wifi_reconnect_enable() are now the SHARED ones in component/wifi_service_config
 * (they erase WIFI_* + park/unpark the shared reconnect monitor); this file no longer defines them. */
extern void FreeRTOS_NetworkDown( NetworkInterface_t * pxNetworkInterface );

/* Single consumer of the driver's WiFi events, blocked on the wifi_ind EventGroup (replacing three
 * callbacks that ran inline in the driver's own thread). Runs in its own task context, so the work is
 * off the driver stack:
 *   DISCONNECT             -> drop the IP stack at the real link loss ("==== network DOWN ====" on time)
 *   FOURWAY_HANDSHAKE_DONE -> re-kick DHCP for a prompt eNetworkUp
 * This is the multi-task notification path: any other task can likewise wait on the same group. */
static void wifi_event_task( void * param )
{
    const EventBits_t mask = WIFI_EVENT_BIT( WIFI_EVENT_DISCONNECT )
                           | WIFI_EVENT_BIT( WIFI_EVENT_FOURWAY_HANDSHAKE_DONE );
    ( void ) param;

    for( ;; )
    {
        EventBits_t b = wifi_event_wait( mask, pdTRUE /*clearOnExit*/, pdFALSE /*any*/, 0xFFFFFFFFu );

        if( b & WIFI_EVENT_BIT( WIFI_EVENT_DISCONNECT ) )
        {
            if( wifi_link_up() )
            {
                wifi_link_set( 0 );
                wifi_reassoc_set( 1 );   /* until the service really re-associates, drop premature ups */
                FreeRTOS_NetworkDown( &xInterface );
                wifi_service_on_event( WIFI_SVC_EV_DISCONNECT );   /* wake the shared reconnect monitor */
            }
        }
        if( b & WIFI_EVENT_BIT( WIFI_EVENT_FOURWAY_HANDSHAKE_DONE ) )
        {
            if( !wifi_link_up() )
                FreeRTOS_NetworkDown( &xInterface );   /* re-kick DHCP; eNetworkUp sets WIFI_CONNECTED_BIT */
        }
    }
}

/* DHCP-only: the endpoint default IPv4 stays 0.0.0.0 (the DHCP lease is the only address). */
static uint8_t ucIPAddress[ 4 ]    = { 0, 0, 0, 0 };
static uint8_t ucNetMask[ 4 ]      = { 255, 255, 255, 0 };
static uint8_t ucGatewayAddress[4] = { 0, 0, 0, 0 };
static uint8_t ucDNSServer[ 4 ]    = { 0, 0, 0, 0 };

/* ---- WIFI_* / DHCP_* env helpers: boot-default WiFi connect + DHCP-lease cache (speed up the
 * connect flow by skipping the scan and the cold DHCP DISCOVER on a known network). ---- */

/* WIFI_SECURITY string <-> rtw_security_t. The scan only ever yields these four (scan_print_handler);
 * map WPA2/WPA3/AES variants to WPA2-AES-PSK (the chip negotiates the actual cipher). */
static rtw_security_t wifi_sec_from_str( const char *s )
{
    if( s == NULL || s[ 0 ] == '\0' )            return RTW_SECURITY_UNKNOWN;
    if( strcmp( s, "OPEN" ) == 0 )               return RTW_SECURITY_OPEN;
    if( strcmp( s, "WEP" ) == 0 )                return RTW_SECURITY_WEP_PSK;
    if( strcmp( s, "WPA2_TKIP" ) == 0 )          return RTW_SECURITY_WPA2_TKIP_PSK;
    if( strcmp( s, "WPA2" ) == 0 || strcmp( s, "WPA2_AES" ) == 0 ||
        strcmp( s, "WPA3" ) == 0 || strcmp( s, "WPA2_WPA3" ) == 0 )
                                                 return RTW_SECURITY_WPA2_AES_PSK;
    return RTW_SECURITY_UNKNOWN;
}
static const char *wifi_sec_to_str( rtw_security_t sec )
{
    switch( sec )
    {
        case RTW_SECURITY_OPEN:          return "OPEN";
        case RTW_SECURITY_WEP_PSK:       return "WEP";
        case RTW_SECURITY_WPA2_TKIP_PSK: return "WPA2_TKIP";
        default:                         return "WPA2_AES";   /* WPA2/WPA3 AES */
    }
}

/* FreeRTOS+TCP IPv4 (network byte order, octet 0 = first) -> "a.b.c.d" in buf (>= 16 bytes). Uses
 * %d (the ROM _rtl_snprintf in main.c does not support %u). */
static void fmt_ipv4( uint32_t ip, char *buf, size_t buflen )
{
    snprintf( buf, buflen, "%d.%d.%d.%d",
              (int)( ip & 0xff ), (int)( ( ip >> 8 ) & 0xff ),
              (int)( ( ip >> 16 ) & 0xff ), (int)( ( ip >> 24 ) & 0xff ) );
}

/* setenv only when the value changed, so the IP task does not rewrite KV/LFS every DHCP event. */
static void env_set_if_changed( const char *name, const char *value )
{
    const char *cur = getenv( name );
    if( cur == NULL || strcmp( cur, value ) != 0 )
        setenv( name, value, 1 );
}

/* Extended-country scan mode (lib_wlan.a): when enabled the driver appends each AP's 2-byte beacon
 * 802.11d Country IE into the scan-result records, at offset 14 (right before the SSID). */
extern void rltk_wlan_enable_scan_with_extended_countryinfo( int enable );

/* Tally of the per-AP Country IEs seen in one scan, for the majority vote in wifi_autodetect_country().
 * Extended-country record layout: [0]=len [1..6]=bssid [7..10]=rssi [11]=sec [12]=wps [13]=channel
 * [14..15]=country [16..]=ssid. */
typedef struct { char cc[ 16 ][ 2 ]; int count[ 16 ]; int num; int aps; } cc_tally_t;

static int country_scan_handler( char *buf, int buflen, char *ssid, void *user_data )
{
    cc_tally_t *t = (cc_tally_t *) user_data;
    int plen = 0;
    ( void ) ssid;

    while( plen < buflen )
    {
        int len = (unsigned char) buf[ plen ];
        char c0, c1;
        int i;
        if( len == 0 ) break;
        t->aps++;
        c0 = buf[ plen + 14 ];
        c1 = buf[ plen + 15 ];
        if( ( c0 >= 'A' && c0 <= 'Z' ) && ( c1 >= 'A' && c1 <= 'Z' ) )   /* AP advertised a Country IE */
        {
            for( i = 0; i < t->num; i++ )
                if( t->cc[ i ][ 0 ] == c0 && t->cc[ i ][ 1 ] == c1 ) { t->count[ i ]++; break; }
            if( i == t->num && t->num < 16 )
            {
                t->cc[ i ][ 0 ] = c0; t->cc[ i ][ 1 ] = c1; t->count[ i ] = 1; t->num++;
            }
        }
        plen += len;
    }
    return 0;
}

/* Country-plan auto-detect: when neither WIFI_COUNTRY nor WIFI_CHANPLAN is provisioned, scan the
 * surrounding APs in extended-country mode, take the MAJORITY 802.11d Country IE, save it to
 * WIFI_COUNTRY, and apply the matching channel plan immediately. wifi_set_channel_plan_by_country_code()
 * triggers a driver reboot; on the reboot WIFI_COUNTRY is set, so wifi_set_country_code() applies the
 * same plan during wifi_on and this detection is skipped. Uses the working async
 * wifi_scan_networks_with_ssid (its result buffer holds every AP heard) - NOT the SDK's sync
 * wifi_scan / wifi_scan_networks_with_extended_countryinfo() / dump_surrounding_country_codes(), whose
 * scan-done never returns in this driver-only image. Call after wifi_on, before associating. */
static void wifi_autodetect_country( void )
{
    cc_tally_t  t;
    const char *ssid;
    int         i, best = -1, best_n = 0;

    if( ( getenv( "WIFI_COUNTRY" ) != NULL ) || ( getenv( "WIFI_CHANPLAN" ) != NULL ) )
        return;   /* regulatory domain already provisioned - leave it alone */

    memset( &t, 0, sizeof( t ) );
    ssid = getenv( "WIFI_SSID" );   /* may be NULL -> broadcast probe; still hears every AP's Country IE */

    /* The scan buffer carries every AP heard regardless of the probed SSID, so one scan tallies all
     * surrounding Country IEs. */
    rltk_wlan_enable_scan_with_extended_countryinfo( 1 );
    wifi_scan_networks_with_ssid( country_scan_handler, &t, 1000,
                                  (char *) ssid, ssid ? (int) strlen( ssid ) : 0 );
    rltk_wlan_enable_scan_with_extended_countryinfo( 0 );

    for( i = 0; i < t.num; i++ )
        if( t.count[ i ] > best_n ) { best_n = t.count[ i ]; best = i; }

    if( best < 0 )
    {
        log_info( "[KM4] country auto-detect: %d APs scanned, none advertised a Country IE - keeping default plan",
                    t.aps );
        return;
    }
    {
        char ccs[ 3 ] = { t.cc[ best ][ 0 ], t.cc[ best ][ 1 ], '\0' };
        log_info( "[KM4] country auto-detect: %d APs, majority %s (%d of %d countries) -> set channel plan + save (reboots)",
                    t.aps, ccs, best_n, t.num );
        setenv( "WIFI_COUNTRY", ccs, 1 );
        wifi_set_channel_plan_by_country_code( (unsigned char *) ccs );   /* applies now; triggers a driver reboot */
    }
}


#if ( ipconfigUSE_IPv6 != 0 )
/* Log every configured IPv6 address on the interface. The fe80:: link-local is set at bring-up; the
 * bWantRA global address appears later, once the Router Advertisement / SLAAC completes. Printed in
 * full (no :: compression) so it can be pasted straight into a ping6. */
static void log_ipv6( const char * pcTag )
{
    NetworkEndPoint_t * px;
    for( px = FreeRTOS_FirstEndPoint( NULL ); px != NULL; px = FreeRTOS_NextEndPoint( NULL, px ) )
    {
        const uint8_t * a = px->ipv6_settings.xIPAddress.ucBytes;
        int i, zero = 1;
        if( px->bits.bIPv6 == 0 ) { continue; }
        for( i = 0; i < ipSIZE_OF_IPv6_ADDRESS; i++ ) { if( a[ i ] != 0 ) { zero = 0; break; } }
        if( zero ) { continue; }   /* global endpoint before SLAAC: address is still :: */
        log_info( "[KM4] IPv6 (%s) %-10s %x:%x:%x:%x:%x:%x:%x:%x", pcTag,
                  ( ( a[ 0 ] == 0xfeU ) && ( ( a[ 1 ] & 0xc0U ) == 0x80U ) ) ? "link-local" : "global",
                  ( a[ 0 ] << 8 ) | a[ 1 ],   ( a[ 2 ] << 8 ) | a[ 3 ],
                  ( a[ 4 ] << 8 ) | a[ 5 ],   ( a[ 6 ] << 8 ) | a[ 7 ],
                  ( a[ 8 ] << 8 ) | a[ 9 ],   ( a[ 10 ] << 8 ) | a[ 11 ],
                  ( a[ 12 ] << 8 ) | a[ 13 ], ( a[ 14 ] << 8 ) | a[ 15 ] );
    }
}

/* 1 once the bWantRA global endpoint has a non-zero (SLAAC-autoconfigured) address. */
static int ipv6_global_set( void )
{
    NetworkEndPoint_t * px;
    for( px = FreeRTOS_FirstEndPoint( NULL ); px != NULL; px = FreeRTOS_NextEndPoint( NULL, px ) )
    {
        const uint8_t * a = px->ipv6_settings.xIPAddress.ucBytes;
        if( ( px->bits.bIPv6 != 0 ) && ( a[ 0 ] != 0xfeU ) )   /* IPv6, not the fe80:: link-local */
        {
            int i;
            for( i = 0; i < ipSIZE_OF_IPv6_ADDRESS; i++ ) { if( a[ i ] != 0 ) { return 1; } }
        }
    }
    return 0;
}

/* The fe80:: link-local endpoint (with its valid address), or NULL. */
static NetworkEndPoint_t * ipv6_linklocal_endpoint( void )
{
    NetworkEndPoint_t * px;
    for( px = FreeRTOS_FirstEndPoint( NULL ); px != NULL; px = FreeRTOS_NextEndPoint( NULL, px ) )
    {
        const uint8_t * a = px->ipv6_settings.xIPAddress.ucBytes;
        if( ( px->bits.bIPv6 != 0 ) && ( a[ 0 ] == 0xfeU ) && ( ( a[ 1 ] & 0xc0U ) == 0x80U ) )
        {
            return px;
        }
    }
    return NULL;
}

/* The bWantRA global (SLAAC) endpoint, or NULL. */
static NetworkEndPoint_t * ipv6_ra_endpoint( void )
{
    NetworkEndPoint_t * px;
    for( px = FreeRTOS_FirstEndPoint( NULL ); px != NULL; px = FreeRTOS_NextEndPoint( NULL, px ) )
    {
        if( ( px->bits.bIPv6 != 0 ) && ( px->bits.bWantRA != 0 ) )
        {
            return px;
        }
    }
    return NULL;
}

/* IPv6 + MLD-snooping interop. The router's bridge does MLD snooping: it forwards an IPv6 multicast
 * group only to ports that announced membership via MLD. FreeRTOS+TCP has no MLD, so the bridge never
 * learns we listen on our solicited-node group and drops the router's Neighbor Solicitation for us ->
 * the router can't resolve our MAC -> inbound ping is 100% loss. (The device DOES receive multicast and
 * answers NS fine - proven: ping works the instant the bridge's MLD snooping is disabled.) Fix:
 * periodically emit an MLDv2 Multicast Listener Report announcing the solicited-node + all-nodes groups
 * so the snooping bridge forwards the NS (and the router's RAs) to us. Hand-crafted (FreeRTOS+TCP has no
 * MLD): IPv6 + a Hop-by-Hop Router-Alert option (mandatory for MLD) + ICMPv6 type 143, to ff02::16. */
static uint16_t icmpv6_checksum( const uint8_t * src, const uint8_t * dst, const uint8_t * msg, uint16_t len )
{
    uint32_t sum = 0;
    int i;
    for( i = 0; i < 16; i += 2 ) { sum += ( ( uint32_t ) src[ i ] << 8 ) | src[ i + 1 ]; }
    for( i = 0; i < 16; i += 2 ) { sum += ( ( uint32_t ) dst[ i ] << 8 ) | dst[ i + 1 ]; }
    sum += len;          /* upper-layer length (high 16 bits are 0 here) */
    sum += 58U;          /* next header = ICMPv6 */
    for( i = 0; ( i + 1 ) < ( int ) len; i += 2 ) { sum += ( ( uint32_t ) msg[ i ] << 8 ) | msg[ i + 1 ]; }
    if( ( len & 1U ) != 0U ) { sum += ( uint32_t ) msg[ len - 1 ] << 8; }
    while( ( sum >> 16 ) != 0U ) { sum = ( sum & 0xffffU ) + ( sum >> 16 ); }
    return ( uint16_t ) ( ~sum );
}

/* Emit one MLDv2 report (a single group record, CHANGE_TO_EXCLUDE = join) for `group`, to ff02::16.
 * One group per report (vs a multi-record report) because RouterOS's MLD snooping only honours the
 * first record - a 3-record report only registered the first group. */
static void send_one_mld( const uint8_t group[ 16 ] )
{
    NetworkEndPoint_t *          px = ipv6_linklocal_endpoint();
    NetworkBufferDescriptor_t *  pxBuf;
    static const uint8_t FF02_16[ 16 ] = { 0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x16 };
    const uint16_t icmp_len = 8U + 20U;        /* MLDv2 hdr(8) + 1 group record(20) */
    const uint16_t plen     = 8U + icmp_len;   /* Hop-by-Hop(8) + ICMPv6 */
    const uint16_t total    = 14U + 40U + plen;
    const uint8_t * ll;
    uint8_t *       b;
    uint16_t        ck;

    if( ( px == NULL ) || ( px->pxNetworkInterface == NULL ) ) { return; }
    pxBuf = pxGetNetworkBufferWithDescriptor( total, 0 );
    if( pxBuf == NULL ) { return; }
    ll = px->ipv6_settings.xIPAddress.ucBytes;
    b  = pxBuf->pucEthernetBuffer;
    ( void ) memset( b, 0, total );

    b[ 0 ] = 0x33; b[ 1 ] = 0x33; b[ 5 ] = 0x16;                      /* Eth dst 33:33:00:00:00:16 */
    ( void ) memcpy( &b[ 6 ], px->xMACAddress.ucBytes, 6 );           /* Eth src our MAC */
    b[ 12 ] = 0x86; b[ 13 ] = 0xdd;                                  /* ethertype IPv6 */
    b[ 14 ] = 0x60;                                                  /* IPv6 ver 6 */
    b[ 18 ] = ( uint8_t ) ( plen >> 8 ); b[ 19 ] = ( uint8_t ) plen;  /* payload length */
    b[ 20 ] = 0; b[ 21 ] = 1;                                        /* next=Hop-by-Hop, hop limit 1 */
    ( void ) memcpy( &b[ 22 ], ll, 16 );                             /* src LL */
    ( void ) memcpy( &b[ 38 ], FF02_16, 16 );                        /* dst ff02::16 */
    b[ 54 ] = 58; b[ 56 ] = 5; b[ 57 ] = 2; b[ 60 ] = 1;            /* Hop-by-Hop Router-Alert(MLD)+PadN */
    b[ 62 ] = 143; b[ 69 ] = 1;                                      /* MLDv2 report, 1 record */
    b[ 70 ] = 4;                                                     /* record: CHANGE_TO_EXCLUDE, 0 src */
    ( void ) memcpy( &b[ 74 ], group, 16 );                          /* multicast address */
    ck = icmpv6_checksum( ll, FF02_16, &b[ 62 ], icmp_len );
    b[ 64 ] = ( uint8_t ) ( ck >> 8 ); b[ 65 ] = ( uint8_t ) ck;

    pxBuf->xDataLength = total;
    pxBuf->pxEndPoint  = px;
    ( void ) px->pxNetworkInterface->pfOutput( px->pxNetworkInterface, pxBuf, pdTRUE );
}

/* Announce our IPv6 multicast group memberships (one MLD report each) so the snooping bridge forwards
 * the matching traffic to us: solicited-node (Neighbor Solicitations), all-nodes ff02::1 (Router
 * Advertisements -> SLAAC global), and ff02::fb (v6 mDNS). */
static void send_mld_report( void )
{
    NetworkEndPoint_t *  px = ipv6_linklocal_endpoint();
    const uint8_t *      ll;
    uint8_t              solnode[ 16 ]  = { 0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0xff, 0, 0, 0 };
    static const uint8_t allnodes[ 16 ] = { 0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01 };
    static const uint8_t mdns[ 16 ]     = { 0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xfb };

    if( px == NULL ) { return; }
    ll = px->ipv6_settings.xIPAddress.ucBytes;
    solnode[ 13 ] = ll[ 13 ]; solnode[ 14 ] = ll[ 14 ]; solnode[ 15 ] = ll[ 15 ];
    /* Space the three sends: pfOutput releases the buffer only after the async driver TX, so three
     * back-to-back pxGetNetworkBufferWithDescriptor() can starve the pool and silently drop the 2nd/3rd. */
    send_one_mld( solnode );
    vTaskDelay( pdMS_TO_TICKS( 500 ) );
    send_one_mld( allnodes );
    vTaskDelay( pdMS_TO_TICKS( 500 ) );
    send_one_mld( mdns );

    /* Once the SLAAC global has formed, join ITS solicited-node group too (the global has a different,
     * random IID -> a different ff02::1:ffXX:XXXX group than the link-local), so the router can resolve
     * and reach the global address, not just the link-local. */
    {
        NetworkEndPoint_t * pxg = ipv6_ra_endpoint();
        if( pxg != NULL )
        {
            const uint8_t * g = pxg->ipv6_settings.xIPAddress.ucBytes;
            if( ( g[ 0 ] | g[ 1 ] ) != 0U )   /* a real (non-zero-prefix) global */
            {
                uint8_t gsol[ 16 ] = { 0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0xff, 0, 0, 0 };
                gsol[ 13 ] = g[ 13 ]; gsol[ 14 ] = g[ 14 ]; gsol[ 15 ] = g[ 15 ];
                vTaskDelay( pdMS_TO_TICKS( 500 ) );
                send_one_mld( gsol );
            }
        }
    }
}

static void ipv6_mld_task( void * pvArg )
{
    int n = 0;
    ( void ) pvArg;
    for( ; ; )
    {
        if( wifi_link_up() && ( ipv6_linklocal_endpoint() != NULL ) )
        {
            send_mld_report();
            if( ++n == 1 )
            {
                log_info( "[KM4] IPv6 MLDv2 report -> ff02::16 (join solicited-node + all-nodes + mDNS; snooping bridge now forwards NS/RA/mDNS)" );
            }
            else if( n == 2 )
            {
                /* The bridge has learned our groups now -> re-run SLAAC so the router's RA reaches us and
                 * the global address forms (the first RA process timed out before the MLD join). */
                NetworkEndPoint_t * ep = ipv6_ra_endpoint();
                if( ep != NULL ) { vRAProcess( pdTRUE, ep ); }
            }
            else if( n == 3 )
            {
                log_ipv6( "after SLAAC restart" );   /* should now show a real-prefix global */
            }
        }
        vTaskDelay( pdMS_TO_TICKS( ( n < 3 ) ? 2000 : 60000 ) );   /* burst 3x@2s, then 60s upkeep */
    }
}
#endif /* ipconfigUSE_IPv6 */

/* Called by the IP task when the interface comes up (DHCP done) or goes down.
 * ipconfigIPv4_BACKWARD_COMPATIBLE=1 -> single-arg hook. */
void vApplicationIPNetworkEventHook( eIPCallbackEvent_t eNetworkEvent )
{
    /* Print "network DOWN" once per down->up cycle: the WiFi DISCONNECT event drops the IP stack (real
     * timing), and the FOURWAY_HANDSHAKE_DONE event calls FreeRTOS_NetworkDown again to re-DHCP - only
     * the first should print. */
    static int s_downLogged = 0;
    static int s_everUp = 0;     /* gates "network DOWN" to a real drop, not the boot DHCP re-kick below */
#if ( ipconfigUSE_IPv6 != 0 )
    static int s_slaacLogged = 0;   /* print the SLAAC global address once, when it autoconfigures */
#endif

    if( eNetworkEvent == eNetworkUp )
    {
        uint32_t ulIP = 0, ulMask = 0, ulGW = 0, ulDNS = 0;
        FreeRTOS_GetAddressConfiguration( &ulIP, &ulMask, &ulGW, &ulDNS );
        /* This hook fires for every endpoint that comes up. Two of those are NOT a real IPv4 connection
         * and must be ignored: (1) the IPv6 link-local endpoint, which comes up at FreeRTOS_IPInit_Multi()
         * before WiFi even connects, and (2) the DHCP "give-up" fallback, where DHCP cannot reach a server
         * so FreeRTOS+TCP reverts the IPv4 endpoint to its (zero) static IP yet STILL fires eNetworkUp.
         * Both leave the IPv4 address 0.0.0.0, so a non-zero leased IPv4 is the test. Honouring a false up
         * would set WIFI_CONNECTED_BIT (masking the outage, so the monitor never re-associates) and s_everUp
         * (which un-silences the boot DHCP re-kick's "network DOWN") - that is the spurious extra UP/DOWN. */
        if( ulIP == 0U )
        {
            return;
        }
        /* Recovering from a DISCONNECT: FreeRTOS_NetworkDown() fires a premature up off the briefly-stale
         * link status with the OLD lease before any re-associate. Ignore it - the monitor clears this once
         * wifi_connect() actually succeeds, so only the genuine post-re-associate DHCP up is honoured. */
        if( wifi_reassoc_pending() )
        {
            return;
        }
        /* Already up -> a duplicate up for the same lease, not a new connection: the IPv6 global endpoint
         * comes up later (after SLAAC) and fires this same endpoint-less hook, as would a DHCP renewal.
         * WIFI_CONNECTED_BIT is cleared only by a real DISCONNECT, so a genuine reconnect still passes.
         * (This was the 2nd spurious "network UP" once IPv6 was enabled.) */
        if( wifi_link_up() )
        {
#if ( ipconfigUSE_IPv6 != 0 )
            /* The bWantRA global IPv6 endpoint comes up here once SLAAC fills its address (the IPv4
             * lease is already up, so this is the "duplicate" path). Print the new address once. */
            if( ipv6_global_set() && ( s_slaacLogged == 0 ) )
            {
                s_slaacLogged = 1;
                log_ipv6( "SLAAC updated" );
            }
#endif
            return;
        }
        s_downLogged = 0;
        s_everUp = 1;
        /* Real lease -> link is connected. The reliable "connected" cue (FOURWAY_HANDSHAKE_DONE is flaky
         * on the driver's autoreconnect); clears the monitor's wait without a redundant takeover. */
        wifi_link_set( 1 );
        /* IPs are in network byte order; byte 0 = first octet. */
        log_info( "[KM4] ==== network UP ====" );
#if ( ipconfigUSE_IPv6 != 0 )
        log_ipv6( "network UP" );   /* link-local now; the global SLAAC address logs separately when it arrives */
#endif
        log_info( "[KM4] DHCP IP   %d.%d.%d.%d",
                    (int)( ulIP & 0xff ), (int)( ( ulIP >> 8 ) & 0xff ),
                    (int)( ( ulIP >> 16 ) & 0xff ), (int)( ( ulIP >> 24 ) & 0xff ) );
        log_info( "[KM4] netmask   %d.%d.%d.%d",
                    (int)( ulMask & 0xff ), (int)( ( ulMask >> 8 ) & 0xff ),
                    (int)( ( ulMask >> 16 ) & 0xff ), (int)( ( ulMask >> 24 ) & 0xff ) );
        log_info( "[KM4] gateway   %d.%d.%d.%d",
                    (int)( ulGW & 0xff ), (int)( ( ulGW >> 8 ) & 0xff ),
                    (int)( ( ulGW >> 16 ) & 0xff ), (int)( ( ulGW >> 24 ) & 0xff ) );
        /* DHCP-provided DNS server list (endpoint settings) + NTP server (DHCP option 42, captured by
         * the FreeRTOS_DHCP.c at-project patch into ulDHCPNtpServerAddress). */
        {
            char     b[ 16 ];
            unsigned i;
            for( i = 0; i < ipconfigENDPOINT_DNS_ADDRESS_COUNT; i++ )
            {
                uint32_t d = xEndPoint.ipv4_settings.ulDNSServerAddresses[ i ];
                if( d != 0U )
                {
                    fmt_ipv4( d, b, sizeof( b ) );
                    log_info( "[KM4] DHCP DNS[%d] %s", (int) i, b );
                }
            }
            /* Optional static DNS override (env DNS_SERVER / DNS_SERVER2). An iPad Personal Hotspot's
             * DNS forwarder does not return AAAA records; pointing at a public resolver that does
             * (e.g. Alibaba 223.5.5.5 / 223.6.6.6) makes IPv6 (AAAA) resolution work. */
            {
                const char * d1 = getenv( "DNS_SERVER" );
                const char * d2 = getenv( "DNS_SERVER2" );
                uint32_t     a1 = ( ( d1 != NULL ) && ( d1[ 0 ] != '\0' ) ) ? FreeRTOS_inet_addr( d1 ) : 0U;
                /* Only an IPv4 literal overrides the IPv4 DNS array. An IPv6 DNS_SERVER (for c-ares
                 * under CARES=ON) yields inet_addr()==0 -- do NOT write that, or it wipes the
                 * DHCP-learned IPv4 DNS and leaves the box with no usable resolver on an IPv4-only link. */
                if( a1 != 0U )
                {
                    xEndPoint.ipv4_settings.ulDNSServerAddresses[ 0 ] = a1;
                    if( ( d2 != NULL ) && ( d2[ 0 ] != '\0' ) )
                    {
                        uint32_t a2 = FreeRTOS_inet_addr( d2 );
                        if( a2 != 0U )
                        {
                            xEndPoint.ipv4_settings.ulDNSServerAddresses[ 1 ] = a2;
                        }
                    }
                    log_info( "[KM4] DNS override -> %s / %s", d1, ( d2 != NULL ) ? d2 : "-" );
                }
            }
            /* c-ares DNS re-point (CARES=ON) now runs from the shared app_on_network_up() below,
             * via app_network_dns_reinit() (strong-overridden in component/c-ares_config). */
            if( ulDHCPNtpServerAddress != 0U )
            {
                fmt_ipv4( ulDHCPNtpServerAddress, b, sizeof( b ) );
                log_info( "[KM4] DHCP NTP    %s", b );
            }
            else
            {
                log_info( "[KM4] DHCP NTP    (none - DHCP option 42 not received)" );
            }
        }
        /* Network services on link-up (SNTP + AWS / QUIC / iperf) via the shared seam;
         * app_network_up_start_services() is strong-overridden for AmebaD just above main(). */
        app_on_network_up();
    }
    else
    {
        if( s_everUp && !s_downLogged )
        {
            s_downLogged = 1;
            log_info( "[KM4] ==== network DOWN ====" );
        }
        /* Forget the DHCP-learned NTP server (option 42): the next association re-derives it from
         * that network's DHCP. Without this, a stale value from a previous network would shadow the
         * SNTP ntp.aliyun.com fallback when the new network advertises no NTP. */
        ulDHCPNtpServerAddress = 0U;
    }
}

/* "ab" -> 0xAB ; tolerant of upper/lower case. */
static unsigned char prvHexByte( const char * p )
{
    unsigned char v = 0;
    int i;
    for( i = 0; i < 2; i++ )
    {
        char c = p[ i ];
        v <<= 4;
        if( ( c >= '0' ) && ( c <= '9' ) ) v |= ( unsigned char )( c - '0' );
        else if( ( c >= 'a' ) && ( c <= 'f' ) ) v |= ( unsigned char )( c - 'a' + 10 );
        else if( ( c >= 'A' ) && ( c <= 'F' ) ) v |= ( unsigned char )( c - 'A' + 10 );
    }
    return v;
}

/*
 * TEST PROBE: country code from surrounding hotspots' beacons.
 *
 * The get_beacon_frame example CANNOT do this: OnBeacon (lib_wlan.a) copies only
 * the 32-byte MAC header + timestamp into BEACON_INFO_T by value and never the
 * IEs, so the beacon Country IE (Element ID 7) is not delivered. The driver's
 * purpose-built path is an "extended country info" scan: with it enabled the
 * driver parses each AP's beacon Country IE and appends the 2-byte country code
 * into every scan-buffer record (at offset 14, right before the SSID). This
 * probe runs that scan and prints the RAW per-AP country field - unlike
 * wifi_get_country_code(), which hides "no AP advertised one" behind a "US"
 * default when fewer than 3 APs agree.
 *
 * Scan-buffer record (extended-country layout):
 *   [0]=len [1..6]=mac [7..10]=rssi [11]=sec [12]=wps_id [13]=channel
 *   [14..15]=country code  [16..]=ssid (len-16 bytes)
 */
static void dump_surrounding_country_codes( void )
{
    enum { HDR = 1 + 6 + 4 + 1 + 1 + 1, CC = 2 };   /* country code at offset 14 */
    scan_buf_arg sb;
    int plen = 0, aps = 0, with_cc = 0;

    sb.buf_len = 25 * 50;                            /* 25 B/record * up to 50 APs */
    sb.buf = (char *) pvPortMalloc( sb.buf_len );
    if( !sb.buf ) { log_error( "[CC] malloc %d failed", sb.buf_len ); return; }
    memset( sb.buf, 0, sb.buf_len );

    rltk_wlan_enable_scan_with_extended_countryinfo( 1 );
    if( wifi_scan( RTW_SCAN_TYPE_ACTIVE, RTW_BSS_TYPE_ANY, &sb ) < 0 )
    {
        log_error( "[CC] scan FAILED" );
        rltk_wlan_enable_scan_with_extended_countryinfo( 0 );
        vPortFree( sb.buf );
        return;
    }
    rltk_wlan_enable_scan_with_extended_countryinfo( 0 );

    log_info( "[CC] ==== surrounding-AP country codes (beacon Country IE) ====" );
    while( plen < sb.buf_len )
    {
        int len = ( unsigned char ) sb.buf[ plen ];
        unsigned char *r = ( unsigned char * )( sb.buf + plen );
        char c0, c1; int ch, ssid_len, i;
        if( len == 0 ) break;
        ch       = r[ 1 + 6 + 4 + 1 + 1 ];          /* channel @ +13 */
        c0       = sb.buf[ plen + HDR ];            /* country[0] @ +14 */
        c1       = sb.buf[ plen + HDR + 1 ];        /* country[1] @ +15 */
        ssid_len = len - HDR - CC;
        aps++;
        if( c0 && c1 ) with_cc++;
        log_info( "[CC] %02x:%02x:%02x:%02x:%02x:%02x ch=%d cc=%c%c ssid=",
                    r[1], r[2], r[3], r[4], r[5], r[6], ch,
                    ( c0 >= 0x20 && c0 < 0x7f ) ? c0 : '-',
                    ( c1 >= 0x20 && c1 < 0x7f ) ? c1 : '-' );
        for( i = 0; i < ssid_len; i++ ) log_info( "%c", sb.buf[ plen + HDR + CC + i ] );
        log_info( "" );
        plen += len;
    }
    log_info( "[CC] ==== %d APs scanned, %d advertised a country code ====",
                aps, with_cc );
    vPortFree( sb.buf );
}

/* Result of the directed SSID scan (filled by scan_print_handler). */
typedef struct {
    int            found;
    unsigned char  channel;
    rtw_security_t security;
    int            rssi;
} found_ap_t;

/*
 * wifi_scan_networks_with_ssid results handler. The scan buffer holds one record
 * per AP found (a FULL all-channel scan, since no pscan is set before it):
 *   [0]=len [1..6]=bssid [7..10]=rssi(int) [11]=enc_alg [12]=wps [13]=channel
 *   [14..]=ssid (len-14 bytes)
 * Prints every AP, and records the target SSID's real channel + security so the
 * connect uses a concrete security type (never RTW_SECURITY_UNKNOWN) - the bug in
 * wifi_connect_simple was leaving security unknown / pscan-restricting the scan.
 */
static int scan_print_handler( char *buf, int buflen, char *target_ssid, void *user_data )
{
    found_ap_t *fa = (found_ap_t *) user_data;
    int plen = 0;

    log_info( "[SCAN] ==== APs (probing SSID '%s') ====", target_ssid );
    while( plen < buflen )
    {
        unsigned char *r = (unsigned char *)( buf + plen );
        int len = r[ 0 ];
        int ssid_len, n, rssi;
        unsigned char enc, ch;
        char ssidbuf[ 33 ];
        if( len == 0 ) break;
        ssid_len = len - 14;
        enc = r[ 11 ];
        ch  = r[ 13 ];
        memcpy( &rssi, buf + plen + 7, 4 );
        n = ( ssid_len > 32 ) ? 32 : ( ssid_len < 0 ? 0 : ssid_len );
        memcpy( ssidbuf, buf + plen + 14, n );
        ssidbuf[ n ] = 0;
        log_info( "[SCAN] %02x:%02x:%02x:%02x:%02x:%02x ch=%d rssi=%d enc=%d ssid=%s",
                    r[1], r[2], r[3], r[4], r[5], r[6], ch, rssi, enc, ssidbuf );
        if( ssid_len == (int) strlen( target_ssid )
            && memcmp( buf + plen + 14, target_ssid, ssid_len ) == 0 )
        {
            /* A dual-band AP shows up as two BSSIDs (2.4 GHz + 5 GHz). Prefer the
             * 5 GHz BSSID (ch>14); within the same band prefer the strongest
             * (rssi is negative dBm -> larger == stronger; RSSI alone fluctuates
             * and could re-pick the other band next boot, so band is the primary
             * key). 5 GHz is the deliberate choice: on a congested 2.4 GHz band
             * the RTL8721D intermittently loses the 4-way EAPOL frames (M3/M4) so
             * the handshake stalls after the pairwise key (the AP completes + even
             * assigns DHCP, but the chip never signals FOURWAY_HANDSHAKE_DONE so
             * wifi_connect blocks) - measured ~1/5 connects on 2.4 GHz vs 5/5 on a
             * clean 5 GHz channel. (The old 2.4-GHz preference was a workaround for
             * a band-steering status-30 reject; the AP no longer steers, so 5 GHz
             * associates fine.) */
            int new_5 = ( ch > 14 );
            int cur_5 = ( fa->channel > 14 );
            int better = !fa->found
                         || ( new_5 && !cur_5 )                  /* 5 GHz beats 2.4 */
                         || ( new_5 == cur_5 && rssi > fa->rssi ); /* else RSSI */
            if( !better ) { plen += len; continue; }
            fa->found   = 1;
            fa->channel = ch;
            fa->rssi    = rssi;
            if( enc == RTW_ENCODE_ALG_NONE )      fa->security = RTW_SECURITY_OPEN;
            else if( enc == RTW_ENCODE_ALG_WEP )  fa->security = RTW_SECURITY_WEP_PSK;
            else if( enc == RTW_ENCODE_ALG_TKIP ) fa->security = RTW_SECURITY_WPA2_TKIP_PSK;
            else if( enc == RTW_ENCODE_ALG_CCMP ) fa->security = RTW_SECURITY_WPA2_AES_PSK;
            else                                  fa->security = RTW_SECURITY_WPA2_AES_PSK;
        }
        plen += len;
    }
    log_info( "[SCAN] ==== end ====" );
    return 0;
}

/* ===== amebad WiFi HAL (wifi_port.h) — the connect mechanics the shared wifi_service drives. The
 * network bring-up (wifi_on/netif/IPInit) + the wifi_event_task stay in wifi_bringup_thread below;
 * only the connect strategy + reconnect monitor were the duplicated policy, now in wifi_service. ===== */
extern "C" {
unsigned wifi_port_task_stack_words( void ) { return 4096; }   /* RTK scan/connect + first-connect bench */
int  wifi_port_enable( void ) { return 0; }                    /* bring-up already done by wifi_bringup_thread */
void wifi_port_autoreconnect_disable( void ) { }               /* RTK driver-only build never autoreconnects */
void wifi_port_disconnect( void ) { wifi_disconnect(); }
int  wifi_port_sec_from_str( const char *s ) { return (int) wifi_sec_from_str( s ); }
const char *wifi_port_sec_to_str( int sec ) { return wifi_sec_to_str( (rtw_security_t) sec ); }

int wifi_port_scan( const char *ssid, uint8_t *chan, int *sec )
{
    /* 6-pass 5 GHz-preferring directed scan: 2.4 GHz here is congested + unreliable for the 4-way, and
     * a probe-response timing race intermittently misses the dual-band AP's 5 GHz BSSID, so re-scan
     * until it shows up (scan_print_handler keeps the best-seen across passes; fa accumulates). */
    found_ap_t fa = { 0, 0, RTW_SECURITY_OPEN, 0 };
    int scan_try;
    for( scan_try = 0; scan_try < 6; scan_try++ )
    {
        log_info( "[KM4] scan for '%s' (prefer 5 GHz, try %d)...", ssid, scan_try + 1 );
        wifi_scan_networks_with_ssid( scan_print_handler, &fa, 1000, (char *) ssid, strlen( ssid ) );
        if( fa.found && fa.channel > 14 ) break;   /* got a 5 GHz BSSID */
        vTaskDelay( pdMS_TO_TICKS( 200 ) );
    }
    if( !fa.found ) return -1;
    if( chan ) *chan = fa.channel;
    if( sec )  *sec = (int) fa.security;
    return 0;
}

int wifi_port_connect( const char *ssid, const char *pass, uint8_t chan, int sec )
{
    extern void rltk_wlan_tx_deauth( unsigned char b_broadcast, unsigned char key_type );
    rtw_security_t security = (rtw_security_t) sec;
    int attempt, ok = 0;

    if( security == RTW_SECURITY_UNKNOWN ) return -1;   /* need the security type from scan/cache to associate */

    /* Broadcast deauth before each attempt flushes the AP's stale station entry for this MAC (the
     * driver-only chip never deauthed on reset), so the assoc is accepted on the first try; the 8-try
     * loop covers the rare transient status-30 reject. pscan restricts the connect probe to `chan`. */
    for( attempt = 1; attempt <= 8; attempt++ )
    {
        u8 c = chan, ps = PSCAN_ENABLE;
        rltk_wlan_tx_deauth( 1, 0 );
        vTaskDelay( pdMS_TO_TICKS( 80 ) );
        if( chan != 0 ) { wifi_set_pscan_chan( &c, &ps, 1 ); }
        if( wifi_connect( (char *) ssid, security, (char *) pass,
                          (int) strlen( ssid ), (int) strlen( pass ), 0, NULL ) == RTW_SUCCESS )
        {
            ok = 1;
            break;
        }
        log_error( "[KM4] wifi_connect attempt %d FAILED, retrying...", attempt );
        vTaskDelay( pdMS_TO_TICKS( 800 ) );
    }
    if( !ok ) return -1;

    /* Post-associate (every connect): full power-save off + clear the reassoc hold so the re-DHCP's
     * genuine network-up is honoured, then re-kick DHCP (fresh DISCOVER -> fast lease -> eNetworkUp
     * -> WIFI_CONNECTED_BIT). */
    wifi_set_power_mode( 0, 0 );
    wifi_disable_powersave();
    wifi_reassoc_set( 0 );
    { int rssi = 0; wifi_get_rssi( &rssi ); log_info( "[KM4] associated; power-save off; RSSI %d dBm", rssi ); }
    FreeRTOS_NetworkDown( &xInterface );

    /* First associate only (guarded so it does not re-run on every reconnect): the upstream Mbed TLS
     * microbenchmark / ROM-mbedTLS KAT diagnostic, which used to sit inline after the initial connect. */
#if defined(AT_PROJECT_CRYPTOBENCH)
    { static int benched = 0; if( !benched ) { benched = 1; extern void mbedtls_bench( void ); mbedtls_bench(); } }
#elif defined(AMEBAD_ROM_KAT)
    { static int katted = 0; if( !katted ) { katted = 1; extern void rom_mbedtls_kat( void ); rom_mbedtls_kat(); } }
#endif
    return 0;
}
}  // extern "C"

static void wifi_bringup_thread( void *param )
{
    char mac_str[ 20 ] = { 0 };   /* wifi_get_mac_address fills "xx:xx:xx:xx:xx:xx" */
    unsigned char mac[ 6 ] = { 0 };
    int i;
    ( void ) param;

    /* Throughput: run KM4 at max clock (default may be lower for power). */
    SystemSetCpuClk( CLK_KM4_200M );

#ifndef AT_PROJECT_QUIC
    /* Seed the wall clock from the hardware RTC: if a prior SNTP sync wrote it and
     * the RTC survived (warm reset / deep sleep), time() reads real local time from
     * here on, so the [KM4] WiFi logs below are already timestamped with wall-clock
     * before SNTP re-syncs. A cold boot leaves the RTC at ~1970 -> seed is skipped.
     * (SNTP is default-on in every non-QUIC image; see the network-up hook.) */
    {
        extern int rtc_time_seed( void );
        rtc_time_seed();
    }
#endif

    /* Set up the KM4 side of the KM4<->KM0 IPC, exactly as the SDK's main() does
     * before WiFi. The WiFi driver self-registers its own data IPC channel (so
     * scan/wifi_on basically work without this), but the GENERAL IPC dispatch
     * (ipc_table_init + the IPC_IRQ handler) is what services KM0-side
     * coordination - including the inter-core RESET handshake (0x480003f8 bit25)
     * that the channel-plan reboot waits on. Without this, that reboot spins in
     * ROM forever. See memory reference-amebad-chplan-reboot-handshake. */
    InterruptRegister( (IRQ_FUN)IPC_INTHandler, IPC_IRQ, (u32)IPCM0_DEV, 5 );
    InterruptEn( IPC_IRQ, 5 );
    ipc_table_init();

    /* LittleFS bring-up + self-test: mount the on-flash FS (auto-formats if the
     * region is blank) and exercise read of the prefilled lfs.bin + a write/
     * read-back round trip. Runs here (scheduler up - the flash device lock and
     * the LFS mutex need FreeRTOS) before WiFi, independent of the link.
     * Dropped from the minimal image (WiFi-connect only); the LFS .text stays
     * linked (XIP-tail pinned), only the self-test call is skipped. */
#ifndef AT_PROJECT_MINIMAL
    {
        extern void lfs_selftest( void );
        lfs_selftest();
    }
#endif


#ifdef AT_PROJECT_PKCS11
    /* corePKCS11 token self-test: needs LittleFS mounted (above) for the PAL + the scheduler
     * (mbedTLS FreeRTOS threading mutexes). Independent of WiFi. See pkcs11_selftest.c. */
    {
        extern void pkcs11_selftest_run( void );   /* component/pkcs11_selftest_config */
        pkcs11_selftest_run();
    }
#endif

#ifdef AMEBAD_CPP_SELFTEST
    /* C++ std::filesystem / iostream self-test over the POSIX VFS. Brings up the
     * LPUART console first (stdin/stdout -> UART3 PB1/PB2 / COM7; stderr stays on
     * LOGUART), then runs the libstdc++ self-test. Pre-WiFi: no network needed. */
    {
        extern void amebad_console_init_lpuart( void );
        extern void cpp_selftest( void );
        amebad_console_init_lpuart();
        cpp_selftest();
    }
#endif

    /* Create the WiFi-indication worker thread before bringing WiFi up. With
     * CONFIG_WIFI_IND_USE_THREAD=1 (platform_opts.h), driver events (CONNECT,
     * FOURWAY_HANDSHAKE_DONE, DISCONNECT) are dispatched to the registered
     * handlers via this worker. The SDK's init_thread() calls it before wifi_on;
     * we'd skipped it, so wifi_connect's join completion (posted by the 4-way-done
     * handler) never fired -> association hung after "start assoc". Scan was
     * unaffected (it uses its own scan-done callback, not this event worker). */
    wifi_manager_init();

    /* SDK main() does this before WiFi (loads efuse-derived boot/cal config into
     * the driver). Matching it for full pre-WiFi parity with the stock SDK. */
    {
        extern void rtw_efuse_boot_write( void );
        rtw_efuse_boot_write();
    }

    log_info( "[KM4] wifi_on(RTW_MODE_STA)..." );
    if( wifi_on( RTW_MODE_STA ) < 0 )
    {
        log_error( "[KM4] wifi_on FAILED" );
        vTaskDelete( NULL );
        return;
    }

#ifdef AT_PROJECT_HCI
    /* BLE DTM image: WiFi is on (DTM needs that for coex), so bring up the BT
     * controller (gnt_bt + bte_init = HCI patch download + reset), then optionally
     * open the HCI bridge. Skip wifi_connect/iperf - DTM only needs WiFi on. */
    {
        extern void bt_dtm_bringup( void );
        extern void bt_dtm_enter_bridge( uint32_t, uint8_t );
        ( void ) bt_dtm_enter_bridge;
        /* Bring up the BT controller: gnt_bt + bte_init downloads the firmware
         * patch over HCI-UART and runs the HCI init sequence (read version / read
         * ROM / set baud / download patch / HCI reset) - i.e. it exercises real
         * HCI command/event round-trips, visible on the console. */
        bt_dtm_bringup();
        /* Hand the console to a host BT-test tool: open the transparent HCI bridge.
         * This takes over COM29 as a raw HCI pipe (the RAM shell is gone until
         * reset). The host syncs on the "entering HCI bridge" marker printed by
         * bt_dtm_enter_bridge() and then drives raw HCI - see project/amebad/hci_test.ps1.
         * host_baud = 1500000 keeps the current console rate so COM29 doesn't re-rate. */
        bt_dtm_enter_bridge( 1500000, 0 );
        vTaskDelete( NULL );
        return;
    }
#endif

#ifdef AT_PROJECT_NIMBLE
    /* NimBLE host image: start the Apache NimBLE host (controller bring-up + host +
     * advertising) on top of our HCI transport (component/amebad/nimble_port). Then FALL THROUGH
     * to the normal WiFi connect + FreeRTOS+TCP + iperf path so WiFi traffic and BLE
     * run concurrently - exercising WiFi/BT coex (P5). nimble_start() spawns
     * its own host/RX tasks and returns. */
    {
        extern void nimble_start( void );
        nimble_start();
        /* fall through -> dump_surrounding_country_codes + wifi_connect + iperf */
    }
#endif

    /* If no regulatory domain was provisioned, scan the surrounding APs, take the majority beacon
     * Country IE, save it to WIFI_COUNTRY, and set the matching channel plan now (reboots, then
     * reconnects with it). Runs before associating so the reboot precedes the connection. */
    wifi_autodetect_country();

    /* Bring up FreeRTOS+TCP BEFORE associating: the driver starts delivering RX
     * (beacons/probes) during association, and the RX bridge needs the buffer
     * pool + IP task to already exist. After wifi_on the driver is running, so
     * the MAC is valid and the netif can be registered. */
    wifi_get_mac_address( mac_str );          /* returns the STRING "xx:xx:.." */
    for( i = 0; i < 6; i++ )                   /* parse to 6 binary bytes */
    {
        mac[ i ] = prvHexByte( &mac_str[ i * 3 ] );
    }
    log_info( "[KM4] MAC %s -> %02x:%02x:%02x:%02x:%02x:%02x",
                mac_str, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5] );

    /* Register netif + MAC with the driver (so RX delivers and LwIP_GetMAC
     * returns the real MAC), then bring up FreeRTOS+TCP BEFORE associating. */
    amebad_register_wlan_netif( WLAN_STA_IDX, mac );
    pxRealtek_FillInterfaceDescriptor( WLAN_STA_IDX, &xInterface );
    /* DHCP-only: endpoint default IPv4 stays 0.0.0.0 (no cached-lease seeding). The old seed was only
     * ever the static-IP fallback on DHCP timeout, which the network-up hook now rejects anyway, and
     * FreeRTOS+TCP only DISCOVERs (never REQUESTs a prior IP) so it bought no fast path. */
    FreeRTOS_FillEndPoint( &xInterface, &xEndPoint, ucIPAddress, ucNetMask,
                           ucGatewayAddress, ucDNSServer, mac );
    xEndPoint.bits.bWantDHCP = pdTRUE;
#if ( ipconfigUSE_IPv6 != 0 )
    /* IPv6 + SLAAC: register the fe80:: link-local + a bWantRA global endpoint now that the interface
     * exists. (The SDK's wifi_conf.c also calls netif_create_ip6_linklocal_address() from wifi_on, but
     * that runs before the interface is registered; this call is the effective one.) */
    { extern void amebad_ipv6_init( void ); amebad_ipv6_init(); }
    /* Periodic MLDv2 reports (see ipv6_mld_task) so the router's MLD-snooping bridge forwards the
     * solicited-node NS / the router's RAs to us - otherwise inbound IPv6 is unreachable. */
    ( void ) xTaskCreate( ipv6_mld_task, "ip6mld", 512, NULL, tskIDLE_PRIORITY + 1, NULL );
#endif
    log_info( "[KM4] FreeRTOS_IPInit_Multi..." );
    FreeRTOS_IPInit_Multi();
    amebad_freertos_tcp_set_ready();

#ifdef AMEBAD_VFS_SELFTEST
    /* POSIX-fd VFS self-test (syscalls retarget): runs here so the socket part has
     * the FreeRTOS+TCP IP task up (the LFS file part works anytime). */
    {
        extern void vfs_selftest( void );
        vfs_selftest();
    }
#endif


    /* Start the consumer of the driver's link-state events. It blocks on the wifi_ind EventGroup
     * (set by wifi_indication for every event) - DISCONNECT marks the IP stack down at the real drop
     * (-> "network DOWN" on time), FOURWAY_HANDSHAKE_DONE re-DHCPs. The group is created in
     * wifi_manager_init() (called earlier in this thread). */
    if( xTaskCreate( wifi_event_task, "wifi_evt", 512, NULL, tskIDLE_PRIORITY + 4, NULL ) != pdPASS )
        log_error( "[KM4] xTaskCreate(wifi_evt) failed" );

    /* Hand off to the shared WiFi provisioning + reconnect service (component/wifi_service_config):
     * it reads WIFI_SSID/WIFI_PASSWORD, connects via the cached channel or a scan, caches the channel,
     * and supervises the link -- driving the amebad wifi_port_* HAL above (the connect mechanics) --
     * replacing the old inline connect strategy + wifi_monitor_thread. Same service bl602 uses. This
     * bring-up task's setup is done, so it exits (freeing its 16 KB stack; the service task carries
     * its own via wifi_port_task_stack_words). */
    wifi_service_start();
    vTaskDelete( NULL );
}

/* ---- shared-startup platform seams (component/app_startup_config) --------------------------------
 * Strong overrides of app_startup.c's weak no-ops: called by app_console_start() (registers) and
 * app_on_network_up() (network services). We're inside the amebad `extern "C"` block, so these keep
 * C linkage and override the weak C symbols. */
#ifndef AT_PROJECT_QUIC
void app_console_register_platform(void)
{
    amebad_monitor_cmds_register();     /* AmebaD port cmds: reboot / dw / ew / flash / efuse / >> / km0 / km4 */
    { extern void wifi_cli_register(void); wifi_cli_register(); }               /* shared `wifi` forget/drop */
    { extern void amebad_wifi_cli_register(void); amebad_wifi_cli_register(); } /* AmebaD-only `iwpriv` */
    { extern void net_cli_register(void); net_cli_register(); }                 /* shared `resolve` / `ip6` */
#ifdef AMEBAD_ENABLE_MINIZIP
    { extern void zip_cli_register(void);  zip_cli_register();  }   /* zip/unzip demo (non-AWS images) */
#endif
#ifdef AMEBAD_SBOOT
    { extern void sboot_cli_register(void); sboot_cli_register(); } /* secure-boot Ed25519 self-test */
#endif
#ifdef AMEBAD_ENABLE_CRT_BUNDLE
    { extern void crt_bundle_cli_register(void); crt_bundle_cli_register(); } /* compressed CA root bundle */
#endif
#ifdef AMEBAD_ENABLE_WPS
    { extern void wps_cli_register(void); wps_cli_register(); }     /* WPS (PBC/PIN) connect test */
#endif
#if defined(AMEBAD_EFUSE_DEBUG) && defined(AMEBAD_TFM_STORAGE)
    { extern void efuse_burn_cli_register(void); efuse_burn_cli_register(); } /* DEBUG eFuse burn (provisioning) */
#endif
#ifdef AT_PROJECT_AWS_IOT
    /* AWS: shared `aws` provisioning command (component/aws_iot_config) + AmebaD TF-M PSA diagnostics
     * (pstest/xiptest) + `ota_http <url>`. aws_provision_init() is a shared weak no-op. */
    {
        extern void aws_provision_init(void);
        extern void aws_cli_register(void);
        extern void aws_platform_diag_register(void);
        extern void ota_http_register(void);
        aws_provision_init();
        aws_cli_register();
        aws_platform_diag_register();
        ota_http_register();
    }
#endif
}
#endif /* !AT_PROJECT_QUIC */

void app_network_up_start_services(void)
{
#ifdef AT_PROJECT_QUIC
    /* QUIC build: run the ngtcp2 + picotls/mbedTLS TLS1.3 handshake self-test against the WSL QUIC
     * server instead of the iperf throughput probes (iperf TUs are not linked in this image). */
    extern void quic_test_start(void);
    quic_test_start();
#else
    /* coreSNTP UTC time-sync (default-on in every non-QUIC image): an async task that fetches UTC and
     * backs newlib's _gettimeofday (time_base.c), so logs / TLS / OTA see real wall-clock time. */
    { extern void sntp_sync_start(void); sntp_sync_start(); }
#if defined(AT_PROJECT_AWS_IOT)
    /* AWS IoT Core MQTT/TLS demo: connect over mutual TLS and publish telemetry (endpoint+cert from
     * LittleFS, key from TF-M Protected Storage). */
    extern void aws_iot_demo_start(void);
    aws_iot_demo_start();
#elif defined(AT_PROJECT_IPERF)
    /* Default throughput probe: device-as-server RX (host connects outbound -> no host firewall rule).
     * The device->host TX + UDP probes stay available for diagnostics. */
    iperf_server_start();
    (void) iperf_client_start;
    (void) iperf_udp_client_start;
    (void) iperf_udp_server_start;
#endif
#endif /* AT_PROJECT_QUIC */
}

int main(void)
{
    log_lock_init();   /* install the FreeRTOS-mutex lock before any log_* (pre-scheduler is fine) */
    log_info("[KM4] at-project image2 - FreeRTOS+TCP DHCP bring-up");
    log_info("[KM4] FreeRTOS kernel %s", tskKERNEL_VERSION_NUMBER);
#if defined(AT_PROJECT_MCUMGR) && defined(AMEBAD_MCUMGR_FW_VERSION)
    /* Boot-visible firmware tag so a mcumgr BLE DFU is verifiable (the new slot prints a new tag). */
    log_info("[mcumgr] firmware tag: %s", AMEBAD_MCUMGR_FW_VERSION);
#endif

#ifdef AMEBAD_TFM_BOOT
    /* Serialize every NS->SPE PSA call: tfm_ns_interface_rtos.c takes this mutex around each
     * dispatch so two NS tasks can't be inside a secure call at once (the SFN partitions share a
     * secure stack - concurrent callers corrupt it). Created here, pre-scheduler. */
    {
        extern uint32_t tfm_ns_interface_init(void);
        tfm_ns_interface_init();
    }
    /* app_km4 is TF-M's Non-Secure world: exercise a real PSA service call. */
    {
        extern void amebad_tfm_psa_selftest(void);
        amebad_tfm_psa_selftest();
    }
#ifdef AMEBAD_TFM_STORAGE
    /* PSA secure storage (ITS [+ PS]) round-trip + reboot-persistence test. */
    {
        extern void amebad_tfm_storage_selftest(void);
        amebad_tfm_storage_selftest();
    }
#endif
#endif

#ifndef AT_PROJECT_QUIC
    /* LittleFS + persisted env + default TZ, then the shared console: the FreeRTOS-Plus-CLI engine
     * (component/monitor_config) over the LOGUART via the AmebaD port + fs/env/time CLI
     * (app_console_start), then the AmebaD platform/feature/AWS commands (app_console_register_platform,
     * defined just above main). Dropped from the QUIC image (the handshake self-test is autonomous):
     * removing the monitor from KM4 image2 shifts the XIP layout to dodge the boot-hang
     * (doc/amebad_xip_layout_boot_hang.md); that image's console stays output-only. */
    app_env_time_init();
    app_console_start();
#else
    /* QUIC self-test image: no console/LFS/env (layout-sensitive). Default TZ only so any timestamped
     * log still reads local time. */
    {
        const char *tz = getenv("TZ");
        if ((tz == NULL) || (tz[0] == '\0'))
            setenv("TZ", "CST-8", 1);
    }
    tzset();
#endif

    /* WiFi+DHCP bring-up runs as its own task (wifi_connect blocks). */
    if (xTaskCreate(wifi_bringup_thread, "wifi_bringup", 4096, NULL,
                    tskIDLE_PRIORITY + 3, NULL) != pdPASS)
        log_error("[KM4] xTaskCreate(wifi_bringup) failed");

    vTaskStartScheduler();

    for (;;) {
    }
}
}  // extern "C"
#endif // CONFIG_PLATFORM_8721D || CONFIG_PLATFORM_8710C

// The bl602/ameba-newer shared prelude (Common.h — selects lwIP vs FreeRTOS+TCP, BT stack headers)
// + the AT framework (C++ classes). AmebaD's branch is self-contained (its own includes above) and
// its Common.h path still assumes lwIP, so keep all of this out of the AmebaD compile.
#if !defined(CONFIG_PLATFORM_8721D) && !defined(CONFIG_PLATFORM_8710C)
#include <stdlib.h>   /* setenv / getenv (default TZ) */
#include <time.h>     /* tzset */
#include "Common.h"
#include "AtTask.h"
#include "TestTask.h"
#include "BoardFlashLock.h"
#include "wifi_service.h"   /* shared WiFi provisioning + reconnect policy */
#include "wifi_port.h"      /* the platform HAL it drives (implemented below) */
#include "app_startup.h"    /* shared startup helpers (app_env_time_init / app_console_start / app_on_network_up) */
#endif

extern "C" {

__attribute__((__weak__)) void *__dso_handle = (void *)&__dso_handle;

#if !defined(CONFIG_PLATFORM_8721D) && !defined(CONFIG_PLATFORM_8710C)
int wifi_do_fast_connect(void) {
    // at::make_shared<at::AtTask>()->startAsync("AtTask", 256 * 12, 4);
    at::make_shared<at::TestTask>()->startAsync("TestTask", 256 * 12, 4);
    return 0;
}
#endif

#if 0  /* WiFi fast-reconnect disabled: FAST_RECONNECT_DATA (flash 0x3FF000) was
        * folded into LittleFS (LFS grown to 1024 KB after the FTL/settings cluster
        * was dropped). Kept for reference; not compiled. */
static int wlan_write_reconnect_data_to_flash(uint8_t *data, uint32_t len) {
    printf("%s() called with data length: %u\n", __func__, len);

    struct wlan_fast_reconnect *reconnect_data = (struct wlan_fast_reconnect *)data;
    if (reconnect_data == NULL || len < sizeof(struct wlan_fast_reconnect)) {
        return -1; // Invalid data or length
    }

    printf("Writing WLAN fast reconnect data to flash...\n");
    printf("PSK ESSID: %s\n", reconnect_data->psk_essid);
    printf("PSK Passphrase: %s\n", reconnect_data->psk_passphrase);
    printf("WPA Global PSK: %s\n", reconnect_data->wpa_global_PSK);
    printf("Channel: %u\n", reconnect_data->channel);
    printf("Security Type: %u\n", reconnect_data->security_type);
#if defined(CONFIG_FAST_DHCP) && CONFIG_FAST_DHCP
    printf("Offer IP: %u\n", reconnect_data->offer_ip);
#endif
#if ATCMD_VER == ATVER_2
    printf("Enable: %u\n", reconnect_data->enable);
#endif
#if defined(CONFIG_ENABLE_WPS) && CONFIG_ENABLE_WPS
    printf("Is WPS AP: %u\n", reconnect_data->is_wps_ap);
#endif
    return 0;
}
#endif // defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C

#if defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2
int write_fast_connect_data_to_flash(unsigned int offer_ip, unsigned int server_ip) {
    (void) offer_ip;
    (void) server_ip;
    return 0;
}
#endif // defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2

#if defined BL602 || defined BL702
static void event_cb_wifi_event(async_input_event_t event, void *private_data) {
    (void) private_data;
    void wifi_event_handler(uint32_t code, uint32_t code2);
    wifi_event_handler(event->code, 0);
}

// async_event has no internal worker thread; it drains its queue when notify_cb fires.
// Defer the drain to the FreeRTOS timer daemon via xTimerPendFunctionCall (same pattern
// as components/wireless/wifi4/wifi4_adapter/bflb_adapter/plat_bouffalo_sdk.c).
static void async_event_dispatch(void *arg1, uint32_t arg2) {
    (void) arg1;
    (void) arg2;
    vTaskSuspendAll();
    async_event_loop();
    xTaskResumeAll();
}

static void async_event_wake(void) {
    TickType_t wait = (xTimerGetTimerDaemonTaskHandle() == xTaskGetCurrentTaskHandle())
                          ? 0
                          : portMAX_DELAY;
    xTimerPendFunctionCall(async_event_dispatch, nullptr, 0, wait);
}
#endif // defined BL602 || defined BL702

#if defined BL606P || defined BL808 || defined BL616 || defined BL616CL
extern void wifi_main(void *param);

static int hal_wifi_start_firmware_task(void) {
    /* enable wifi clock */
    GLB_PER_Clock_UnGate(GLB_AHB_CLOCK_IP_WIFI_PHY | GLB_AHB_CLOCK_IP_WIFI_MAC_PHY | GLB_AHB_CLOCK_IP_WIFI_PLATFORM);
    GLB_AHB_MCU_Software_Reset(GLB_AHB_MCU_SW_WIFI);

    /* Enable wifi irq */
    extern void interrupt0_handler(void);
    bflb_irq_attach(WIFI_IRQn, (irq_callback)interrupt0_handler, NULL);
    bflb_irq_enable(WIFI_IRQn);

    xTaskCreate(wifi_main, (char *)"fw", WIFI_STACK_SIZE, NULL, TASK_PRIORITY_FW, NULL);

    return 0;
}
#endif // defined BL606P || defined BL808 || defined BL616 || defined BL616CL

#if defined BL616 || defined BL616CL
// FreeRTOS-Plus-TCP bring-up (component/bouffalo_sdk/freertos_tcp_port): register the
// STA interface + DHCP endpoint + start the IP task. Called once the WiFi driver is up.
extern "C" void bouffalo_tcp_bringup(void);
#endif // defined BL616 || defined BL616CL

#if defined BL602 || defined BL702
static void bl602_conn_event(int connected);      // connect result (CONNECTED/DISCONNECT) -> wifi_port_connect
#endif

#if defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616 || defined BL616CL
void wifi_event_handler(uint32_t code, uint32_t code2) {
    (void) code2;
    switch (code) {
        case CODE_WIFI_ON_INIT_DONE: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_INIT_DONE\n", __func__);
#if defined BL602 || defined BL702
            wifi_conf_t conf = { .country_code = "CN" };
            wifi_mgmr_start_background(&conf);
#endif // defined BL602 || defined BL702
#if defined BL606P || defined BL808 || defined BL616 || defined BL616CL
            wifi_mgmr_init();
            wifi_mgmr_set_country_code((char *)"CN");
#endif // defined BL606P || defined BL808 || defined BL616 || defined BL616CL
            break;
        }
        case CODE_WIFI_ON_MGMR_DONE: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_MGMR_DONE\n", __func__);
#if defined BL616 || defined BL616CL
            // Bring up FreeRTOS-Plus-TCP now (driver is up, MAC is valid): register
            // the STA interface + DHCP endpoint + start the IP task. The stack then
            // waits on phy-link (wifi_mgmr_sta_state_get) before sending DHCP, so it
            // is safe to init before association.
            bouffalo_tcp_bringup();
#endif // defined BL616 || defined BL616CL
            wifi_do_fast_connect();
#if defined BL602 || defined BL702
            // Start the shared WiFi provisioning + reconnect service (component/wifi_service_config).
            // It runs in its own task (a blocking connect must not run here: wifi_event_handler runs
            // with the scheduler suspended and wifi_mgmr_sta_connect() blocks on a semaphore). It reads
            // WIFI_SSID/WIFI_PASSWORD from env, connects via the cached channel or a scan, and supervises
            // the link -- driving the bl602 wifi_port_* HAL above. Same service AmebaD uses.
            wifi_service_start();
#endif // defined BL602 || defined BL702
            break;
        }
        case CODE_WIFI_ON_SCAN_DONE: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_SCAN_DONE\n", __func__);
            break;
        }
        case CODE_WIFI_ON_CONNECTING: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_CONNECTING\n", __func__);
            break;
        }
        case CODE_WIFI_ON_CONNECTED: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_CONNECTED\n", __func__);
#if defined BL602 || defined BL702
            bl602_conn_event(1);   // wake wifi_port_connect: association succeeded
            wifi_service_on_event(WIFI_SVC_EV_CONNECTED);
#endif
            break;
        }
        case CODE_WIFI_ON_PRE_GOT_IP: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_PRE_GOT_IP\n", __func__);
            break;
        }
        case CODE_WIFI_ON_GOT_IP: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_GOT_IP\n", __func__);
#if defined BL602 || defined BL702
            // GOT_IP fires only on BL602/BL702 (bl602_netif.c), after DHCP. Notify the wifi service,
            // then run the shared network-up services via app_on_network_up() (DNS re-point for c-ares
            // + coreSNTP + AWS telemetry). SNTP/telemetry are idempotent, so repeated GOT_IP is safe.
            wifi_service_on_event(WIFI_SVC_EV_GOTIP);
            app_on_network_up();
#endif
            break;
        }
        case CODE_WIFI_ON_DISCONNECT: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_DISCONNECT\n", __func__);
#if defined BL602 || defined BL702
            bl602_conn_event(0);   // wake wifi_port_connect: association failed/dropped
            wifi_service_on_event(WIFI_SVC_EV_DISCONNECT);
#endif
            break;
        }
        case CODE_WIFI_ON_AP_STARTED: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_AP_STARTED\n", __func__);
            break;
        }
        case CODE_WIFI_ON_AP_STOPPED: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_AP_STOPPED\n", __func__);
            break;
        }
        case CODE_WIFI_ON_AP_STA_ADD: {
            printf("[APP] [EVT] [AP] [ADD] %lld\n", xTaskGetTickCount());
            break;
        }
        case CODE_WIFI_ON_AP_STA_DEL: {
            printf("[APP] [EVT] [AP] [DEL] %lld\n", xTaskGetTickCount());
            break;
        }
        default: {
            printf("[APP] [EVT] Unknown code %u\n", code);
        }
    }
}

#if defined BL602 || defined BL702
extern void wifi_task_create(void);
#endif

static void on_tcpip_init_done(void *arg) {
    (void)arg;
#if defined BL602 || defined BL702
    // wifi_task_create() starts the wifi_main thread (which services the IPC
    // command queue used by wifi_mgmr) AND posts CODE_WIFI_ON_INIT_DONE
    // internally. Without this, bl_send_reset() etc. enqueue commands that
    // nothing processes and block forever on the event-group ACK.
    wifi_task_create();
#endif // defined BL602 || defined BL702
}

#if defined(CONFIG_NIMBLE_HOST)
// Apache NimBLE host bring-up — shared nimble_start() (component/nimble_config/
// nimble_app.c). Runs from a task because it brings the on-chip controller up (which
// creates its own task) and starts the NimBLE host task — both need the scheduler running.
extern "C" void nimble_start(void);
static void bflb_nimble_boot_task(void *arg) {
    (void)arg;
    nimble_start();
    vTaskDelete(NULL);
}
#endif // CONFIG_NIMBLE_HOST

#if defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616 || defined BL616CL
// Runs AFTER vTaskStartScheduler() — lwIP's tcpip_init() used to defer on_tcpip_init_done
// to its tcpip thread (post-scheduler). Without lwIP we call it from this one-shot task,
// because on BL602/BL702 it does wifi_task_create(), which uses FreeRTOS software timers;
// pre-scheduler the timer daemon task does not exist yet and xTimerGetTimerDaemonTaskHandle
// asserts. (Other Bouffalo chips: on_tcpip_init_done body is a no-op.)
static void bflb_net_bootstrap_task(void *arg) {
    (void)arg;
    on_tcpip_init_done(NULL);
    vTaskDelete(NULL);
}
#endif

#if defined BL602 || defined BL702
// bl602 WiFi HAL (wifi_port.h): the driver mechanics behind the shared wifi_service policy. The
// connect strategy (env creds, cached-channel direct connect, scan fallback, offline guard,
// reconnect monitor, `wifi forget`) lives ONCE in component/wifi_service_config; this file only
// bridges to wifi4's wifi_mgmr_* -- same seam AmebaD uses (its port is below, Stage B).

// ---- scan-complete signal (directed active scan) ----
static TaskHandle_t s_bl602_scan_waiter;
static void bl602_scan_done_cb(void *data, void *param) {
    (void)data; (void)param;
    TaskHandle_t t = s_bl602_scan_waiter;
    if (t != NULL) { s_bl602_scan_waiter = NULL; xTaskNotifyGive(t); }
}

// ---- connect-result signal: wifi_mgmr_sta_connect is async, so block on the driver's
//      CODE_WIFI_ON_CONNECTED (ok) / CODE_WIFI_ON_DISCONNECT (fail) event (fed from wifi_event_handler). ----
static TaskHandle_t s_bl602_conn_waiter;
static volatile int s_bl602_conn_ok;
static void bl602_conn_event(int connected) {
    TaskHandle_t t = s_bl602_conn_waiter;
    if (t != NULL) { s_bl602_conn_ok = connected; s_bl602_conn_waiter = NULL; xTaskNotifyGive(t); }
}

extern "C" {
unsigned wifi_port_task_stack_words(void) { return 1024; }   // wifi4 scan/connect is light
int wifi_port_enable(void) {
    // wifi_mgmr_sta_enable() loads the efuse MAC into wlan_sta.mac (else the STA vif has a ZERO MAC
    // and the AP never answers the auth frame). Must precede connect.
    (void)wifi_mgmr_sta_enable();
    return 0;
}
void wifi_port_autoreconnect_disable(void) {
    // Kill wifi4's native timer-based reconnect so wifi_service is the single reconnect authority.
    wifi_mgmr_sta_autoconnect_disable();
}
int wifi_port_scan(const char *ssid, uint8_t *chan, int *sec) {
    wifi_mgmr_ap_item_t ap;
    s_bl602_scan_waiter = xTaskGetCurrentTaskHandle();
    wifi_mgmr_scan_adv(NULL, bl602_scan_done_cb, NULL, 0, NULL, (char *)ssid, 0, 0);
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(3000));
    if (wifi_mgmr_scan_ap((char *)ssid, &ap) != 0) return -1;   // not found
    if (chan) *chan = ap.channel;
    if (sec)  *sec = -1;   // wifi4 auto-detects security on connect (no WIFI_SECURITY cache)
    return 0;
}
int wifi_port_connect(const char *ssid, const char *pass, uint8_t chan, int sec) {
    (void)sec;   // wifi4 picks the security from the beacon; nothing to pass through
    s_bl602_conn_ok = 0;
    s_bl602_conn_waiter = xTaskGetCurrentTaskHandle();
    // A non-zero chan makes the wifi4 connect probe just that one channel for the BSSID.
    wifi_mgmr_sta_connect(NULL, (char *)ssid, (char *)pass, NULL, NULL, 0, chan);
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(6000)) == 0) {
        s_bl602_conn_waiter = NULL;   // timeout -> failure
        return -1;
    }
    return s_bl602_conn_ok ? 0 : -1;
}
void wifi_port_disconnect(void) {
    wifi_mgmr_sta_disconnect();
}
int wifi_port_sec_from_str(const char *s) { (void)s; return -1; }   // wifi4 auto-detects security
const char *wifi_port_sec_to_str(int sec) { (void)sec; return ""; }

// AWS IoT platform hook (component/aws_iot_config): the WiFi STA MAC is the Thing name / MQTT client
// ID / telemetry topic. Fills "aabbccddeeff" + NUL (out_len must be >= 13). 0 = ok.
int aws_get_mac(char *out, size_t out_len) {
    uint8_t mac[6];
    if (out == NULL || out_len < 13) return -1;
    if (wifi_mgmr_sta_mac_get(mac) != 0) return -1;
    snprintf(out, out_len, "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return 0;
}
}  // extern "C"
#endif

/* ---- shared-startup platform seams (component/app_startup_config) --------------------------------
 * Strong overrides of app_startup.c's weak no-ops for the Bouffalo platform. We're inside the outer
 * `extern "C"` block, so these keep C linkage and override the weak C symbols. app_console_start()
 * calls the first; app_on_network_up() (from the GOT_IP handler) calls the second. */
void app_console_register_platform(void)
{
    bouffalo_monitor_cmds_register();   // reboot
    zip_cli_register();                 // zip/unzip (minizip over the VFS)
    { extern void net_cli_register(void); net_cli_register(); }   // shared `resolve` / `ip6`
#if !defined(BL602)
    // iperf is stripped on bl602 (its ~16 KB static buffers are reclaimed for the AWS TLS handshake
    // heap; see component/bouffalo_sdk/CMakeLists.txt). Other Bouffalo chips keep it.
    iperf_cli_register();               // iperf [server|tx|udp] throughput test
#endif
#if defined BL602 || defined BL702
    // The shared wifi_service `wifi` (forget/drop) + minimal AWS IoT telemetry (`aws`): only BL602/BL702
    // supply the wifi_port_* HAL / fit the telemetry heap. Same libraries + credential model as AmebaD.
    { extern void wifi_cli_register(void); wifi_cli_register(); }
    { extern void aws_cli_register(void); aws_cli_register(); }
#endif
#ifdef AT_CRYPTO_SELFTESTS
    // Opt-in diagnostic image (make CRYPTO_SELFTESTS=ON): the Mbed TLS crypto self-tests. Default OFF
    // because all three together overflow bl602's xip_memory region (~100 KB over).
    bench_cli_register();               // Mbed TLS crypto microbenchmark ([MCB] scores)
    crt_bundle_cli_register();          // Mozilla CA root bundle info/verify/addca
    pkcs11_cli_register();              // corePKCS11 token self-test (keygen/sign/verify)
#endif
}

void app_network_up_start_services(void)
{
    // coreSNTP UTC time sync (idempotent). BL602/BL702 reach here from GOT_IP; the other Bouffalo chips
    // start SNTP from main() instead (they don't post GOT_IP).
    sntp_sync_start();
#if defined BL602 || defined BL702
    // Auto-start AWS telemetry once DHCP is up (bl602). Idempotent + a no-op until provisioned, so it
    // Just Works after `aws endpoint/ca/cert/key ...`; the connect loop self-heals until SNTP supplies
    // real UTC (needed for the mbedTLS server-cert validity check).
    { extern void aws_telemetry_start(void); aws_telemetry_start(); }
#endif
#ifdef AT_PROJECT_QUIC
    { extern void quic_test_start(void); quic_test_start(); }
#endif
}

int main(void) {
    board_init();
    log_lock_init();   // install the rxi-logger FreeRTOS-mutex lock before any log_* (SDK LOG_*/rfparam)
    // Lock bottom 3 MB of flash against accidental writes; must run after
    // flash driver init (board_init) and before anything that writes flash
    // (OTA / EasyFlash). Top 1 MB stays writable for PSM (LittleFS).
    at::lockFirmwareFlash();
#if defined BL606P || defined BL808 || defined BL616 || defined BL616CL
    hal_wifi_start_firmware_task();
#endif // defined BL606P || defined BL808 || defined BL616 || defined BL616CL
#if defined BL602 || defined BL702
    async_event_init(async_event_wake);
    async_register_event_filter(EV_WIFI, event_cb_wifi_event, NULL);
#endif // defined BL602 || defined BL702
    if (0 != rfparam_init(0, NULL, 0)) {
        printf("rfparam_init() failed\n");
        return 0;
    }
    bflb_mtd_init();
    // Mount LittleFS (PSM) via the shared VFS hook + load POSIX env from /KV + default TZ to CST-8 --
    // the shared startup helper (component/app_startup_config), identical to AmebaD. vfs_lfs_mount()
    // is idempotent, so the lazy mount from the first open() is a no-op after this.
    app_env_time_init();

#if defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616 || defined BL616CL
    // lwIP has been removed on Bouffalo — the network stack is switching to
    // FreeRTOS-Plus-TCP. Defer the old lwIP tcpip_init callback (BL602/BL702:
    // wifi_task_create()) to a post-scheduler task — it must not run in main()
    // because it uses FreeRTOS timers (see bflb_net_bootstrap_task above).
    // TODO(freertos-plus-tcp): FreeRTOS_IPInit_Multi() + the NetworkInterface bring-up.
    xTaskCreate(bflb_net_bootstrap_task, (char *)"net_boot", 512, NULL,
                tskIDLE_PRIORITY + 1, NULL);
#else
    // ameba platforms still run lwIP.
    tcpip_init(on_tcpip_init_done, NULL);
#endif

    // TEMP heap-budget measurement (AWS-fit): drop AtTask (12 KB stack over uart1) to reclaim ~12 KB
    // of idle heap. The FreeRTOS-Plus-CLI console on uart0 (below) stays; only the AT-command surface
    // over uart1 is gone. Gate behind an AWS build flag once the budget is confirmed.
    // at::make_shared<at::AtTask>()->startAsync("AtTask", 256 * 12, 4);

    // Interactive FreeRTOS-Plus-CLI console on uart0 (same command set as AmebaD: ls/cat/setenv/date/...):
    // the shared engine (app_console_start = monitor + fs/env/time) then the Bouffalo platform + feature
    // commands (app_console_register_platform, defined above main). AtTask keeps uart1, so no RX contention.
    app_console_start();

    // coreSNTP UTC time sync. BL602/BL702 post CODE_WIFI_ON_GOT_IP (bl602_netif.c), so they start
    // SNTP from that event (above) — after DHCP, like AmebaD. The other Bouffalo chips (bl616/cl,
    // bouffalo_netif.c) don't post GOT_IP, so start it here; it self-retries until the network is up.
#if !defined(BL602) && !defined(BL702)
    sntp_sync_start();
#endif

#if defined(CONFIG_NIMBLE_HOST)
    xTaskCreate(bflb_nimble_boot_task, (char *)"nimble_boot", 1024, NULL, 3, NULL);
#endif // CONFIG_NIMBLE_HOST

    vTaskStartScheduler();
    while (1) {}
    return 0;
}
#endif // defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616 || defined BL616CL

// Ameba-NEWER (amebadplus / amebagreen2 / lite / smart) app hook, called by the Realtek SDK's
// own main(). AmebaD (8721D/8710C) is NOT here anymore — it supplies its own main() above.
#if defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2
void app_pre_example(void) {
    // Lock the firmware flash region (bottom 3 MB) against accidental writes
    // from glitches / brownouts. Must happen before WLAN / BT / OTA start.
    at::lockFirmwareFlash();
    at::unlockFirmwareFlash();
}
#endif // defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2

} // extern "C"

#if defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616 || defined BL616CL
namespace std {

void __throw_bad_function_call() { my_assert("__throw_bad_function_call" == nullptr); }

} // namespace std
#endif // defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616 || defined BL616CL
