/*
 * AmebaD KM4 (HP) image2 — WiFi + FreeRTOS-Plus-TCP DHCP bring-up.
 *
 * Goal of this slice: associate to an AP (WPA2-PSK) and obtain an IP via
 * FreeRTOS-Plus-TCP's DHCP client, proving the FreeRTOS+TCP stack runs on
 * AmebaD over the Realtek WiFi driver (replacing lwip's IP role). Sockets /
 * TCP / apps are follow-on.
 *
 * Flow (in wifi_bringup_thread):
 *   wifi_on(STA) -> wifi_connect(AIoT) -> get MAC -> register netif with driver
 *   -> FreeRTOS+TCP: fill interface + endpoint(DHCP) -> FreeRTOS_IPInit_Multi
 *   -> DHCP -> vApplicationIPNetworkEventHook(eNetworkUp) prints the IP.
 */
#include "ameba_soc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "wifi_conf.h"            /* wifi_on / wifi_connect / wifi_get_mac_address */
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Routing.h"     /* NetworkInterface_t / NetworkEndPoint_t / FillEndPoint */

/* console: FreeRTOS-Plus-CLI over the LOGUART (component/amebad/monitor) */
extern void amebad_monitor_start(void);

/* FreeRTOS+TCP Realtek glue (component/amebad/freertos_tcp_port) */
extern NetworkInterface_t *pxRealtek_FillInterfaceDescriptor(BaseType_t xEMACIndex,
                                                             NetworkInterface_t *pxInterface);
extern void amebad_register_wlan_netif(int idx, unsigned char *mac);
extern void amebad_freertos_tcp_set_ready(void);
extern void iperf_client_start(void);
extern void iperf_server_start(void);
extern void iperf_udp_client_start(void);
extern void iperf_udp_server_start(void);

#define WLAN_STA_IDX  0

static NetworkInterface_t xInterface;
static NetworkEndPoint_t  xEndPoint;

/* DHCP-only: start with 0.0.0.0 so the endpoint requests an address. */
static const uint8_t ucIPAddress[ 4 ]    = { 0, 0, 0, 0 };
static const uint8_t ucNetMask[ 4 ]      = { 255, 255, 255, 0 };
static const uint8_t ucGatewayAddress[4] = { 0, 0, 0, 0 };
static const uint8_t ucDNSServer[ 4 ]    = { 0, 0, 0, 0 };

/* Called by the IP task when the interface comes up (DHCP done) or goes down.
 * ipconfigIPv4_BACKWARD_COMPATIBLE=1 -> single-arg hook. */
void vApplicationIPNetworkEventHook( eIPCallbackEvent_t eNetworkEvent )
{
    if( eNetworkEvent == eNetworkUp )
    {
        uint32_t ulIP = 0, ulMask = 0, ulGW = 0, ulDNS = 0;
        FreeRTOS_GetAddressConfiguration( &ulIP, &ulMask, &ulGW, &ulDNS );
        /* IPs are in network byte order; byte 0 = first octet. */
        DiagPrintf( "[KM4] ==== network UP ====\n\r" );
        DiagPrintf( "[KM4] DHCP IP   %d.%d.%d.%d\n\r",
                    (int)( ulIP & 0xff ), (int)( ( ulIP >> 8 ) & 0xff ),
                    (int)( ( ulIP >> 16 ) & 0xff ), (int)( ( ulIP >> 24 ) & 0xff ) );
        DiagPrintf( "[KM4] netmask   %d.%d.%d.%d\n\r",
                    (int)( ulMask & 0xff ), (int)( ( ulMask >> 8 ) & 0xff ),
                    (int)( ( ulMask >> 16 ) & 0xff ), (int)( ( ulMask >> 24 ) & 0xff ) );
        DiagPrintf( "[KM4] gateway   %d.%d.%d.%d\n\r",
                    (int)( ulGW & 0xff ), (int)( ( ulGW >> 8 ) & 0xff ),
                    (int)( ( ulGW >> 16 ) & 0xff ), (int)( ( ulGW >> 24 ) & 0xff ) );
#ifdef AT_PROJECT_QUIC
        /* QUIC build: run the ngtcp2 + wolfSSL TLS1.3 handshake self-test against
         * the WSL QUIC server instead of the iperf throughput probes (iperf TUs
         * are not linked in this image). */
        extern void quic_test_start( void );
        quic_test_start();
#elif !defined(AT_PROJECT_PKCS11) && !defined(AT_PROJECT_CRYPTOBENCH)
        /* Default = TCP TX (device -> WSL `iperf -s`); also exercises the
         * rewritten FreeRTOS+TCP RX bridge via the ACK path. The UDP TX/RX and
         * TCP-sink probes are kept available for throughput diagnostics.
         * The PKCS11 / crypto-bench images drop the iperf TUs (focused tests). */
        iperf_client_start();
        ( void ) iperf_udp_client_start;
        ( void ) iperf_udp_server_start;
        ( void ) iperf_server_start;
#endif
    }
    else
    {
        DiagPrintf( "[KM4] ==== network DOWN ====\n\r" );
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
 * probe runs that scan and prints the RAW per-AP country field — unlike
 * wifi_get_country_code(), which hides "no AP advertised one" behind a "US"
 * default when fewer than 3 APs agree.
 *
 * Scan-buffer record (extended-country layout):
 *   [0]=len [1..6]=mac [7..10]=rssi [11]=sec [12]=wps_id [13]=channel
 *   [14..15]=country code  [16..]=ssid (len-16 bytes)
 */
extern void rltk_wlan_enable_scan_with_extended_countryinfo( int );
static void dump_surrounding_country_codes( void )
{
    enum { HDR = 1 + 6 + 4 + 1 + 1 + 1, CC = 2 };   /* country code at offset 14 */
    scan_buf_arg sb;
    int plen = 0, aps = 0, with_cc = 0;

    sb.buf_len = 25 * 50;                            /* 25 B/record * up to 50 APs */
    sb.buf = (char *) pvPortMalloc( sb.buf_len );
    if( !sb.buf ) { DiagPrintf( "[CC] malloc %d failed\n\r", sb.buf_len ); return; }
    memset( sb.buf, 0, sb.buf_len );

    rltk_wlan_enable_scan_with_extended_countryinfo( 1 );
    if( wifi_scan( RTW_SCAN_TYPE_ACTIVE, RTW_BSS_TYPE_ANY, &sb ) < 0 )
    {
        DiagPrintf( "[CC] scan FAILED\n\r" );
        rltk_wlan_enable_scan_with_extended_countryinfo( 0 );
        vPortFree( sb.buf );
        return;
    }
    rltk_wlan_enable_scan_with_extended_countryinfo( 0 );

    DiagPrintf( "\n\r[CC] ==== surrounding-AP country codes (beacon Country IE) ====\n\r" );
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
        DiagPrintf( "[CC] %02x:%02x:%02x:%02x:%02x:%02x ch=%d cc=%c%c ssid=",
                    r[1], r[2], r[3], r[4], r[5], r[6], ch,
                    ( c0 >= 0x20 && c0 < 0x7f ) ? c0 : '-',
                    ( c1 >= 0x20 && c1 < 0x7f ) ? c1 : '-' );
        for( i = 0; i < ssid_len; i++ ) DiagPrintf( "%c", sb.buf[ plen + HDR + CC + i ] );
        DiagPrintf( "\n\r" );
        plen += len;
    }
    DiagPrintf( "[CC] ==== %d APs scanned, %d advertised a country code ====\n\r",
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
 * connect uses a concrete security type (never RTW_SECURITY_UNKNOWN) — the bug in
 * wifi_connect_simple was leaving security unknown / pscan-restricting the scan.
 */
static int scan_print_handler( char *buf, int buflen, char *target_ssid, void *user_data )
{
    found_ap_t *fa = (found_ap_t *) user_data;
    int plen = 0;

    DiagPrintf( "[SCAN] ==== APs (probing SSID '%s') ====\n\r", target_ssid );
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
        DiagPrintf( "[SCAN] %02x:%02x:%02x:%02x:%02x:%02x ch=%d rssi=%d enc=%d ssid=%s\n\r",
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
             * wifi_connect blocks) — measured ~1/5 connects on 2.4 GHz vs 5/5 on a
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
    DiagPrintf( "[SCAN] ==== end ====\n\r" );
    return 0;
}

static void wifi_bringup_thread( void *param )
{
    char mac_str[ 20 ] = { 0 };   /* wifi_get_mac_address fills "xx:xx:xx:xx:xx:xx" */
    unsigned char mac[ 6 ] = { 0 };
    int i;
    ( void ) param;

    /* Throughput: run KM4 at max clock (default may be lower for power). */
    SystemSetCpuClk( CLK_KM4_200M );

    /* Set up the KM4 side of the KM4<->KM0 IPC, exactly as the SDK's main() does
     * before WiFi. The WiFi driver self-registers its own data IPC channel (so
     * scan/wifi_on basically work without this), but the GENERAL IPC dispatch
     * (ipc_table_init + the IPC_IRQ handler) is what services KM0-side
     * coordination — including the inter-core RESET handshake (0x480003f8 bit25)
     * that the channel-plan reboot waits on. Without this, that reboot spins in
     * ROM forever. See memory reference-amebad-chplan-reboot-handshake. */
    InterruptRegister( (IRQ_FUN)IPC_INTHandler, IPC_IRQ, (u32)IPCM0_DEV, 5 );
    InterruptEn( IPC_IRQ, 5 );
    ipc_table_init();

    /* LittleFS bring-up + self-test: mount the on-flash FS (auto-formats if the
     * region is blank) and exercise read of the prefilled lfs.bin + a write/
     * read-back round trip. Runs here (scheduler up — the flash device lock and
     * the LFS mutex need FreeRTOS) before WiFi, independent of the link. */
    {
        extern void lfs_selftest( void );
        lfs_selftest();
    }

#ifdef AT_PROJECT_PKCS11
    /* corePKCS11 token self-test: needs LittleFS mounted (above) for the PAL + the scheduler
     * (mbedTLS FreeRTOS threading mutexes). Independent of WiFi. See pkcs11_selftest.c. */
    {
        extern void amebad_pkcs11_selftest( void );
        amebad_pkcs11_selftest();
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

    DiagPrintf( "[KM4] wifi_on(RTW_MODE_STA)...\n\r" );
    if( wifi_on( RTW_MODE_STA ) < 0 )
    {
        DiagPrintf( "[KM4] wifi_on FAILED\n\r" );
        vTaskDelete( NULL );
        return;
    }

#ifdef AT_PROJECT_HCI
    /* BLE DTM image: WiFi is on (DTM needs that for coex), so bring up the BT
     * controller (gnt_bt + bte_init = HCI patch download + reset), then optionally
     * open the HCI bridge. Skip wifi_connect/iperf — DTM only needs WiFi on. */
    {
        extern void bt_dtm_bringup( void );
        extern void bt_dtm_enter_bridge( uint32_t, uint8_t );
        ( void ) bt_dtm_enter_bridge;
        /* Bring up the BT controller: gnt_bt + bte_init downloads the firmware
         * patch over HCI-UART and runs the HCI init sequence (read version / read
         * ROM / set baud / download patch / HCI reset) — i.e. it exercises real
         * HCI command/event round-trips, visible on the console. */
        bt_dtm_bringup();
        /* Hand the console to a host BT-test tool: open the transparent HCI bridge.
         * This takes over COM29 as a raw HCI pipe (the RAM shell is gone until
         * reset). The host syncs on the "entering HCI bridge" marker printed by
         * bt_dtm_enter_bridge() and then drives raw HCI — see project/amebad/hci_test.ps1.
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
     * run concurrently — exercising WiFi/BT coex (P5). amebad_nimble_start() spawns
     * its own host/RX tasks and returns. */
    {
        extern void amebad_nimble_start( void );
        amebad_nimble_start();
        /* fall through -> dump_surrounding_country_codes + wifi_connect + iperf */
    }
#endif

    /* TEST: can we read country code from surrounding hotspots' beacons?
     * Runs a clean active scan before associating.
     * TEMP-DISABLED: the extended-country-info scan mode appears to hang here;
     * wifi_connect_simple does its own plain SSID scan, so skip this diagnostic. */
    /* dump_surrounding_country_codes(); */

    /* Bring up FreeRTOS+TCP BEFORE associating: the driver starts delivering RX
     * (beacons/probes) during association, and the RX bridge needs the buffer
     * pool + IP task to already exist. After wifi_on the driver is running, so
     * the MAC is valid and the netif can be registered. */
    wifi_get_mac_address( mac_str );          /* returns the STRING "xx:xx:.." */
    for( i = 0; i < 6; i++ )                   /* parse to 6 binary bytes */
    {
        mac[ i ] = prvHexByte( &mac_str[ i * 3 ] );
    }
    DiagPrintf( "[KM4] MAC %s -> %02x:%02x:%02x:%02x:%02x:%02x\n\r",
                mac_str, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5] );

    /* Register netif + MAC with the driver (so RX delivers and LwIP_GetMAC
     * returns the real MAC), then bring up FreeRTOS+TCP BEFORE associating. */
    amebad_register_wlan_netif( WLAN_STA_IDX, mac );
    pxRealtek_FillInterfaceDescriptor( WLAN_STA_IDX, &xInterface );
    FreeRTOS_FillEndPoint( &xInterface, &xEndPoint, ucIPAddress, ucNetMask,
                           ucGatewayAddress, ucDNSServer, mac );
    xEndPoint.bits.bWantDHCP = pdTRUE;
    DiagPrintf( "[KM4] FreeRTOS_IPInit_Multi...\n\r" );
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

    /* Connect to "AIoT" the explicit way (wifi_connect_simple leaves security
     * UNKNOWN and pscan-restricts its own scan): FIRST do a directed FULL
     * all-channel scan (no pscan set yet) so we reliably find the AP + its real
     * channel + security, PRINT every result, THEN pscan that channel and connect
     * with the discovered security type. */
    {
        const char *kSsid = "Zeng";
        found_ap_t  fa = { 0, 0, RTW_SECURITY_OPEN, 0 };
        u8 ch;

        /* PMF / 802.11w: keep it OFF (0 = no MFP support). We tried advertising
         * MFP-capable (1) and MFP-required (2) via rltk_set_pmf so the AP could keep
         * management-protection=required, but the RTL8721D closed driver's PMF *data
         * path* is unreliable: auth + 4-way handshake + key install all succeed, yet
         * unicast data (the DHCP DISCOVER) intermittently never gets through, so DHCP
         * stays 0.0.0.0. Measured on a PMF=required AP: MFP=1 ~1/3 boots got an IP,
         * MFP=2 0/2; vs MFP=0 + AP management-protection=allowed which is 2/2 stable.
         * So we disable PMF on the chip and run the AP as 'allowed' (its wpa3 clients
         * still use PMF). CONFIG_IEEE80211W is enabled in the driver (wlan/autoconf.h);
         * the value enum is comment-only in wifi_conf.h, hence the literal. */
        wifi_set_mfp_support( 0 );

        /* The directed all-channel scan intermittently captures only the 2.4 GHz
         * BSSID and misses the dual-band AP's 5 GHz one (probe-response timing).
         * 2.4 GHz is congested here and unreliable for the 4-way handshake (see
         * scan_print_handler), so re-scan a few times until the 5 GHz BSSID shows
         * up; settle for 2.4 GHz only if 5 GHz never appears. fa accumulates
         * across passes (the 5 GHz-preferring handler keeps the best seen so far). */
        {
            int scan_try;
            for( scan_try = 0; scan_try < 6; scan_try++ )
            {
                DiagPrintf( "[KM4] scan for '%s' (prefer 5 GHz, try %d)...\n\r",
                            kSsid, scan_try + 1 );
                wifi_scan_networks_with_ssid( scan_print_handler, &fa, 1000,
                                              (char *) kSsid, strlen( kSsid ) );
                if( fa.found && fa.channel > 14 ) break;   /* got a 5 GHz BSSID */
                vTaskDelay( pdMS_TO_TICKS( 200 ) );
            }
        }
        if( !fa.found )
        {
            DiagPrintf( "[KM4] '%s' not found in scan — abort\n\r", kSsid );
            vTaskDelete( NULL );
            return;
        }
        DiagPrintf( "[KM4] found '%s' ch=%d rssi=%d sec=0x%x; pscan + connect...\n\r",
                    kSsid, fa.channel, fa.rssi, (unsigned) fa.security );
        ch = fa.channel;
        /* This AP (MikroTik) intermittently answers the first assoc with status
         * 30 = "rejected temporarily; try again later". It's transient, so retry
         * a handful of times (re-arming pscan each attempt — the driver consumes
         * the pscan channel list on each connect) before giving up. */
        {
            int attempt, ok = 0;
            /* The AP rejects the first assoc with status 30 because it still holds
             * this MAC's stale station entry (the driver-only chip never sent a
             * deauth on reset, so the AP keeps the old session across our reboot).
             * Sending a broadcast deauth before each attempt flushes that entry, so
             * the assoc is accepted on the first try — verified 3/3 (no status 30,
             * no retry needed). b_broadcast=1, key_type=0. The 8-try loop below now
             * just covers the rare genuinely-transient reject. */
            extern void rltk_wlan_tx_deauth( unsigned char b_broadcast, unsigned char key_type );
            for( attempt = 1; attempt <= 8; attempt++ )
            {
                u8 c = ch, ps = PSCAN_ENABLE;
                DiagPrintf( "[KM4] tx_deauth(broadcast) before attempt %d\n\r", attempt );
                rltk_wlan_tx_deauth( 1, 0 );
                vTaskDelay( pdMS_TO_TICKS( 80 ) );
                wifi_set_pscan_chan( &c, &ps, 1 );
                if( wifi_connect( (char *) kSsid, fa.security, "123456789",
                                  (int) strlen( kSsid ), 9, 0, NULL ) == RTW_SUCCESS )
                {
                    ok = 1;
                    break;
                }
                DiagPrintf( "[KM4] wifi_connect attempt %d FAILED, retrying...\n\r", attempt );
                vTaskDelay( pdMS_TO_TICKS( 800 ) );
            }
            if( !ok )
            {
                DiagPrintf( "[KM4] wifi_connect FAILED (gave up)\n\r" );
                vTaskDelete( NULL );
                return;
            }
        }
    }
    /* Throughput: fully disable power-save (IPS + LPS off). */
    wifi_set_power_mode( 0, 0 );
    wifi_disable_powersave();
    {
        int rssi = 0;
        wifi_get_rssi( &rssi );
        DiagPrintf( "[KM4] associated; power-save off; RSSI %d dBm; waiting for DHCP...\n\r", rssi );
    }

    /* Upstream Mbed TLS v3.6.4 microbenchmark ([MCB] scores) — the apples-to-apples
     * successor to the removed wolfCrypt [WCB] bench. Runs here at 200 MHz, after
     * associate, so the timebase matches the old wolfCrypt run. Replaces the ROM-mbedTLS
     * KAT in this image (the ROM mbedTLS 2.4.0 TU is not linked alongside upstream). */
#if defined(AT_PROJECT_CRYPTOBENCH)
    {
        extern void mbedtls_bench( void );
        mbedtls_bench();
    }
#elif !defined(AT_PROJECT_QUIC) && !defined(AT_PROJECT_PKCS11)
    /* ROM mbedTLS KAT (pure software crypto, independent of the link). Skipped in the
     * QUIC/PKCS11 images (their diagnostic TUs aren't linked). The wolfCrypt benchmarks
     * were removed with the wolfSSL retirement (crypto is now mbedTLS/picotls). */
    {
        extern void rom_mbedtls_kat( void );
        rom_mbedtls_kat();
    }
    /* HW IPSEC engine compare is gated off: CRYPTO_Init faults (SecureFault) — the
     * crypto peripheral (~0x48012000) is secure-attributed and unreachable from
     * this NS driver-only image. Re-enable to retest. */
#if 0
    {
        extern void rom_hwcrypto_compare( void );
        rom_hwcrypto_compare();
    }
#endif
#endif /* !AT_PROJECT_QUIC && !AT_PROJECT_PKCS11 */

    vTaskDelete( NULL );
}

int main(void)
{
    DiagPrintf("\n\r[KM4] at-project image2 — FreeRTOS+TCP DHCP bring-up\n\r");
    DiagPrintf("[KM4] FreeRTOS kernel %s\n\r", tskKERNEL_VERSION_NUMBER);

#ifdef AMEBAD_TFM_BOOT
    /* app_km4 is TF-M's Non-Secure world: exercise a real PSA service call.
     * Done here (pre-scheduler, single-threaded) so the bare-metal NS dispatch
     * is safe. See tfm_psa_selftest.c. */
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

    /* Console: FreeRTOS-Plus-CLI over the LOGUART (component/amebad/monitor). KM4 owns the
     * console at boot; `>>` / `km0` hand it to KM0. It POLLS the UART (LOGUART_Readable) and
     * never enables the RX IRQ, so it sidesteps the ROM "Irq 3 Fun Not Assign" storm an
     * enabled-but-unhandled RX line causes. Dropped from the QUIC image (the handshake
     * self-test is autonomous): removing the monitor from KM4 image2 shrinks .text and shifts
     * the XIP layout to dodge the boot-hang (doc/amebad_xip_layout_boot_hang.md); that image's
     * console stays output-only (DiagPrintf TX is unaffected). */
#ifndef AT_PROJECT_QUIC
    amebad_monitor_start();
#endif

    /* WiFi+DHCP bring-up runs as its own task (wifi_connect blocks). */
    if (xTaskCreate(wifi_bringup_thread, "wifi_bringup", 4096, NULL,
                    tskIDLE_PRIORITY + 3, NULL) != pdPASS)
        DiagPrintf("[KM4] xTaskCreate(wifi_bringup) failed\n\r");

    vTaskStartScheduler();

    for (;;) {
    }
}
