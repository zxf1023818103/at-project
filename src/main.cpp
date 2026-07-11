// ===================================================================================
// Shared AT-firmware example entry point (every platform compiles this file). It carries:
//   - the shared, platform-neutral startup skeleton (app_startup_run) + main();
//   - the Bouffalo (BL602/BL616/...) and Ameba-newer (RTL8711/...) platform bodies, each behind its
//     own chip #ifdef (these were component/at_app_port; folded back here to drop a thin indirection).
// The AmebaD KM4 body stays in its OWN translation unit -- component/amebad/app_port/amebad_app.cpp --
// because it is large and XIP-layout-sensitive; it strong-overrides the same weak hooks this file
// declares. So AmebaD's compile of this file sees NONE of the Bouffalo/Ameba-newer code (its chip
// macro CONFIG_PLATFORM_8721D gates it out) and gets the weak hook defaults below.
//
// Conditionals here are chip macros (for the folded-in platform bodies) plus FEATURE/capability gates:
//   AT_PROJECT_QUIC   - console-less self-test image (drops the console libs)
//   AT_SDK_OWNS_MAIN  - the Realtek SDK provides main() (Ameba-newer); this file exports app_pre_example
// ===================================================================================
#include "app_startup.h"   // the shared hook contract (app_startup_run / app_env_time_init / hooks)

#include <stdio.h>    /* printf */
#include <stdlib.h>   /* getenv / setenv (default TZ) */
#include <time.h>     /* tzset */

// True on the Bouffalo family (any BL chip). Used to gate the shared weak hook defaults out of the
// Bouffalo compile: Bouffalo's STRONG hook overrides are in THIS same TU (folded in below), so a weak
// default beside them would be a duplicate definition. AmebaD/Ameba-newer keep the weak defaults (their
// strong overrides, if any, are in a different TU -> normal weak/strong link resolution).
#if defined(BL602) || defined(BL702) || defined(BL606P) || defined(BL808) || defined(BL616) || defined(BL616CL)
#define AT_MAIN_BOUFFALO 1
#endif

// ===================================================================================
// Bouffalo / Ameba-newer platform port (was component/at_app_port/at_app_port.cpp)
// ===================================================================================

// The bl602/ameba-newer shared prelude (Common.h — selects lwIP vs FreeRTOS+TCP, BT stack headers)
// + the AT framework (C++ classes). AmebaD's branch is self-contained (its own TU) and its Common.h
// path still assumes lwIP, so keep all of this out of the AmebaD compile.
#if !defined(CONFIG_PLATFORM_8721D) && !defined(CONFIG_PLATFORM_8710C)
#include "Common.h"
#include "AtTask.h"
#include "TestTask.h"
#include "BoardFlashLock.h"
#include "board_flash_lock.h"   /* cross-platform flash write-protect: board_flash_lock_fw / _log_status / flash_cli_register */
#include "wifi_service.h"   /* shared WiFi provisioning + reconnect policy */
#include "wifi_port.h"      /* the platform HAL it drives (implemented below) */
#include "FreeRTOS_IP.h"    /* vApplicationIPNetworkEventHook / eNetworkUp / FreeRTOS_GetAddressConfiguration */
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
/* WiFi events now dispatch SYNCHRONOUSLY via the wifi4 adapter's platform_post_event (bflb_async_event
 * removed, aligning to AmebaD's direct wifi_indication dispatch). Register this shim with
 * platform_register_event; it runs in the POSTING task's context (the wifi_mgmr state-machine task, or
 * the FreeRTOS+TCP IP task for GOT_IP) -- wifi_event_handler stays non-blocking (sets bits / notifies /
 * spawns tasks). platform_event_t / platform_register_event come from platform_al.h (transitively
 * included via the SDK wifi headers). */
static void event_cb_wifi_event(platform_event_t *event, void *code2) {
    (void) code2;
    void wifi_event_handler(uint32_t code, uint32_t code2);
    wifi_event_handler((uint32_t) event->code, 0);
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
            // Network-up services now start from the FreeRTOS+TCP eNetworkUp hook
            // (vApplicationIPNetworkEventHook above), unified with AmebaD -- not from this SDK event.
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
/* Network-up trigger, unified with AmebaD: the FreeRTOS+TCP IP task fires eNetworkUp when DHCP
 * completes (bl602_tcp_bringup -> at_netif_bringup -> FreeRTOS_IPInit_Multi). This is now the single
 * network-up cue on both platforms -- it replaces the SDK's CODE_WIFI_ON_GOT_IP -> app_on_network_up().
 * The IPv6 link-local endpoint + the DHCP give-up fallback both fire this with a 0.0.0.0 lease, and the
 * IPv6 global (SLAAC) endpoint re-fires it once up, so guard on a non-zero lease and dedup per up/down. */
extern void at_netif_signal_got_ip( uint32_t );   /* drive bl602 wifi_mgmr out of ipObtaining */
extern void at_netif_on_network_up( void );        /* shared network-up body (component/freertos-plus-tcp_config) */
extern void at_netif_on_network_down( void );
void vApplicationIPNetworkEventHook( eIPCallbackEvent_t eNetworkEvent )
{
    static int s_up = 0;

    if( eNetworkEvent == eNetworkUp )
    {
        uint32_t ulIP = 0, ulMask = 0, ulGW = 0, ulDNS = 0;
        FreeRTOS_GetAddressConfiguration( &ulIP, &ulMask, &ulGW, &ulDNS );
        if( ( ulIP == 0U ) || ( s_up != 0 ) )
        {
            return;   /* link-local IPv6 / DHCP give-up (0.0.0.0), or a duplicate up for this lease */
        }
        s_up = 1;
        /* Advance the WiFi driver out of "ipObtaining" (bl602 wifi_mgmr_api_ip_got via the netif ops).
         * FreeRTOS+TCP owns DHCP, so the driver never sees its own got-IP cue; without it the driver
         * self-disconnects (~ip-obtaining timeout) and the link churns every ~30 s. */
        at_netif_signal_got_ip( ulIP );
        wifi_service_on_event( WIFI_SVC_EV_GOTIP );
        at_netif_on_network_up();   /* shared: [NET] DHCP IP/mask/GW/DNS/NTP + app_on_network_up */
    }
    else   /* eNetworkDown: reset the dedup so a genuine reconnect is honoured */
    {
        if( s_up != 0 )
        {
            s_up = 0;
            at_netif_on_network_down();
        }
    }
}

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

#if defined(AT_PROJECT_NIMBLE)
// Apache NimBLE host bring-up — shared nimble_start() (component/nimble_config/
// nimble_app.c). Runs from a task because it brings the on-chip controller up (which
// creates its own task) and starts the NimBLE host task — both need the scheduler running.
extern "C" void nimble_start(void);
static void bflb_nimble_boot_task(void *arg) {
    (void)arg;
    nimble_start();
    vTaskDelete(NULL);
}
#endif // AT_PROJECT_NIMBLE

// Runs AFTER vTaskStartScheduler() — lwIP's tcpip_init() used to defer on_tcpip_init_done
// to its tcpip thread (post-scheduler). Without lwIP we call it from this one-shot task,
// because on BL602/BL702 it does wifi_task_create(), which uses FreeRTOS software timers;
// pre-scheduler the timer daemon task does not exist yet and xTimerGetTimerDaemonTaskHandle
// asserts. (Other Bouffalo chips: on_tcpip_init_done body is a no-op.)
static void bflb_net_bootstrap_task(void *arg) {
    (void)arg;
#if !defined(AT_PROJECT_AWS) && !defined(AT_PROJECT_MINIMAL)
    // Portable LittleFS self-test (component/littlefs_config/lfs_selftest.c) — the same boot
    // write/read round-trip amebad runs. Post-scheduler here (LFS_THREADSAFE mutex); skipped in
    // AWS + MINIMAL images, matching amebad (connect-and-run image, boot time + heap headroom).
    { extern void lfs_selftest(void); lfs_selftest(); }
#endif
    on_tcpip_init_done(NULL);
    vTaskDelete(NULL);
}

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

/* ---- shared-startup platform seams: strong overrides for the Bouffalo platform -------------------
 * Strong overrides of the weak no-ops below. We're inside the outer `extern "C"` block, so these keep
 * C linkage and override the weak C symbols. app_console_start() calls the first; app_on_network_up()
 * (from the GOT_IP handler) calls the second. */
void app_console_register_platform(void)
{
    bouffalo_monitor_cmds_register();   // reboot
    flash_cli_register();               // shared `flash` lock/unlock/status (component/flash_cli_config)
#if defined(BL602) && defined(AT_PROJECT_AWS)
    // Shared `ota_http <url>` (bflb_flash + pt_table + inline-xz slot writer). bl602 only: it needs the
    // mbedTLS TLS transport for https, which - like aws_iot - is only linked with the AWS stack (AWS=ON).
    { extern void ota_http_register(void); ota_http_register(); }
#endif
#ifndef AT_PROJECT_AWS_IOT
    zip_cli_register();                 // zip/unzip (minizip over the VFS). Dropped in the bl602 AWSOTA
                                        // image: gc-sections then strips minizip's ~4 KB of .bss, which
                                        // (living below __HeapBase) grows the heap for the AWS TLS handshake.
#endif
    { extern void net_cli_register(void); net_cli_register(); }   // shared `resolve` / `ip6`
#if defined(AT_PROJECT_NIMBLE)
    { extern void ble_tp_cli_register(void); ble_tp_cli_register(); }   // shared `bletp` BLE throughput test
#endif
#if (!defined(BL602) || !defined(AT_PROJECT_AWS)) && !defined(AT_PROJECT_MINIMAL)
    // iperf is stripped on bl602 AWS images (its ~16 KB static buffers are reclaimed for the
    // AWS TLS handshake heap; see component/bouffalo_sdk/CMakeLists.txt) and in MINIMAL images
    // (WiFi-connect only, like amebad). Everything else keeps it.
    iperf_cli_register();               // iperf [server|tx|udp] throughput test
#endif
#if defined BL602 || defined BL702
    // The shared wifi_service `wifi` (forget/drop) + minimal AWS IoT telemetry (`aws`): only BL602/BL702
    // supply the wifi_port_* HAL / fit the telemetry heap. Same libraries + credential model as AmebaD.
    { extern void wifi_cli_register(void); wifi_cli_register(); }
#ifdef AT_PROJECT_AWS
    { extern void aws_cli_register(void); aws_cli_register(); }
#endif
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
#if (defined BL602 || defined BL702) && defined(AT_PROJECT_AWS)
    // Auto-start AWS once DHCP is up (bl602). Idempotent + a no-op until provisioned, so it Just Works
    // after `aws endpoint/ca/cert/key ...`; the connect loop self-heals until SNTP supplies real UTC
    // (needed for the mbedTLS server-cert validity check). AWSOTA=ON runs the shared agent+telemetry+OTA
    // orchestration (aws_iot_demo.c); default runs the lean telemetry-only client (aws_telemetry.c).
#ifdef AT_PROJECT_AWS_IOT
    { extern void aws_iot_demo_start(void); aws_iot_demo_start(); }
#else
    { extern void aws_telemetry_start(void); aws_telemetry_start(); }
#endif
#endif
#ifdef AT_PROJECT_QUIC
    { extern void quic_test_start(void); quic_test_start(); }
#endif
}

/* app_startup_run() (the shared skeleton, below) calls these in canonical order; the common middle
 * (env/time + console) is in the shared helper. These are the Bouffalo-specific steps. */

// Chip + logger + flash write-lock + WiFi firmware/event wiring + RF params + flash MTD. Runs first.
void app_platform_boot_init(void)
{
    board_init();
    log_lock_init();   // install the rxi-logger FreeRTOS-mutex lock before any log_* (SDK LOG_*/rfparam)
    // Lock bottom 3 MB of flash against accidental writes; must run after
    // flash driver init (board_init) and before anything that writes flash
    // (OTA / EasyFlash). Top 1 MB stays writable for PSM (LittleFS). The
    // cross-platform helper (component/flash_cli_config, over board_flash_protect
    // in bouffalo_sdk/board_flash_port) replaces the old at::lockFirmwareFlash;
    // board_flash_log_status() prints the resulting lock state at boot.
    board_flash_lock_fw();
    board_flash_log_status();
#if defined BL606P || defined BL808 || defined BL616 || defined BL616CL
    hal_wifi_start_firmware_task();
#endif // defined BL606P || defined BL808 || defined BL616 || defined BL616CL
#if defined BL602 || defined BL702
    // Register the app WiFi-event handler with the wifi4 adapter's direct dispatcher (no async queue).
    // wifi_mgmr also registers its internal handler; platform_post_event fans out to both.
    platform_register_event(EV_WIFI, event_cb_wifi_event, NULL);
#endif // defined BL602 || defined BL702
    if (0 != rfparam_init(0, NULL, 0))
        printf("rfparam_init() failed\n");   // non-fatal: keep booting so the console still comes up
    bflb_mtd_init();
}

// Seed the wall clock from the retained hardware RTC (bflb_rtc HBN counter + the KV RTC_REF reference)
// so logs read real local time before SNTP re-syncs -- the bl602 analog of AmebaD's rtc_time_seed() in
// its WiFi bring-up. Needs env loaded (app_env_time_init) for RTC_REF; the strong port is
// component/bouffalo_sdk/rtc_time_port (a no-op elsewhere via the shared seam).
void app_platform_rtc_seed(void)
{
    extern int rtc_time_seed(void);
    rtc_time_seed();
}

// Bring up FreeRTOS-Plus-TCP off a deferred task (lwIP removed on Bouffalo). Must not run inline in the
// startup path because it uses FreeRTOS timers; FreeRTOS_IPInit_Multi() + the NetworkInterface bring-up
// run once WiFi associates, and network-up is signalled by the eNetworkUp hook (same trigger as AmebaD).
void app_platform_wifi_bringup(void)
{
    xTaskCreate(bflb_net_bootstrap_task, (char *)"net_boot", 512, NULL,
                tskIDLE_PRIORITY + 1, NULL);
}

// Pre-scheduler service tasks not gated on network-up.
void app_platform_pre_scheduler_services(void)
{
    // coreSNTP UTC time sync. BL602/BL702 post CODE_WIFI_ON_GOT_IP (bl602_netif.c), so they start SNTP
    // from that event (network-up hook) -- after DHCP, like AmebaD. The other Bouffalo chips (bl616/cl,
    // bouffalo_netif.c) don't post GOT_IP, so start it here; it self-retries until the network is up.
#if !defined(BL602) && !defined(BL702)
    sntp_sync_start();
#endif
#if defined(AT_PROJECT_NIMBLE)
    xTaskCreate(bflb_nimble_boot_task, (char *)"nimble_boot", 1024, NULL, 3, NULL);
#endif // AT_PROJECT_NIMBLE
}
#endif // Bouffalo family (BL602/BL702/BL606P/BL808/BL616/BL616CL)

// Ameba-NEWER (amebadplus / amebagreen2 / lite / smart) app hook, called by the Realtek SDK's
// own main() (AT_SDK_OWNS_MAIN). AmebaD (8721D/8710C) is NOT here — it supplies its own main() and
// body in component/amebad/app_port/amebad_app.cpp.
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
#endif // Bouffalo family

// ===================================================================================
// Shared startup skeleton (was component/app_startup_config) + main()
// ===================================================================================
extern "C" {

extern void vTaskStartScheduler(void);

/* Call from each platform's network-up handler once the link is genuinely up: (re)point the DNS
 * resolver then start the app services. Only weak refs, so it is safe to link into a console-less image
 * (the AmebaD QUIC image calls it from its network-up hook). */
void app_on_network_up(void)
{
    app_network_dns_reinit();          /* c-ares re-point (strong in component/c-ares_config) or no-op */
    app_network_up_start_services();   /* SNTP / AWS / QUIC / iperf (strong per platform) */
}

/* (Re)point the DNS resolver. No-op default; component/c-ares_config supplies a strong override when
 * linked (CARES=ON). Never strong-defined in this TU, so it is unconditional. */
__attribute__((weak)) void app_network_dns_reinit(void) { }

/* Weak no-op hook defaults. Compiled EXCEPT on Bouffalo, where the strong overrides above share this TU
 * (a weak+strong pair in one TU is a redefinition). AmebaD's strong overrides live in amebad_app.cpp
 * (a different TU), so normal weak/strong link resolution applies there. */
#ifndef AT_MAIN_BOUFFALO
__attribute__((weak)) void app_platform_boot_init(void) { }
__attribute__((weak)) void app_platform_rtc_seed(void) { }
__attribute__((weak)) void app_platform_wifi_bringup(void) { }
__attribute__((weak)) void app_platform_pre_scheduler_services(void) { }
__attribute__((weak)) void app_console_register_platform(void) { }
__attribute__((weak)) void app_network_up_start_services(void) { }
#endif

#ifdef AT_PROJECT_QUIC
/* Console-less startup for the QUIC self-test image: same platform hooks as app_startup_run() minus the
 * LittleFS/env/console path (that image drops the console libs); default TZ only so timestamps read
 * local time. The QUIC handshake self-test starts from the network-up hook. */
void app_startup_run_headless(void)
{
    app_platform_boot_init();
    {
        const char *tz = getenv("TZ");
        if ((tz == NULL) || (tz[0] == '\0'))
            setenv("TZ", "CST-8", 1);
    }
    tzset();
    app_platform_wifi_bringup();
    vTaskStartScheduler();
    for (;;) { }
}
#endif // AT_PROJECT_QUIC

/* The console/env skeleton. Only for images where OUR main() drives startup AND the console is present:
 * NOT the QUIC image (drops console libs) and NOT Ameba-newer (its SDK owns main() + never calls these,
 * so keeping their monitor_start / *_cli_register references out of that link is required). These gates
 * reproduce what the old libapp_startup.a archive did by on-demand member pull. */
#if !defined(AT_PROJECT_QUIC) && !defined(AT_SDK_OWNS_MAIN)

/* Symbols we call resolve at the final image link (forward-extern style). vfs_lfs_mount() is the shared
 * mount name on both platforms. */
extern void *vfs_lfs_mount(void);
extern void  env_init(void);
extern void  monitor_start(void);
extern void  fs_cli_register(void);
extern void  env_cli_register(void);
extern void  time_cli_register(void);

/* Mount LittleFS + load persisted POSIX env + default TZ to CST-8 (kept if already set) + tzset(). */
void app_env_time_init(void)
{
    void *lfs = vfs_lfs_mount();
    printf("[LFS] mount %s\n", lfs ? "OK" : "FAILED");
    if (lfs != NULL)
        env_init();   /* load KV/ into newlib environ (the --wrap=setenv hook then persists runtime setenv) */

    /* Default the wall clock + log timestamps to China Standard Time (UTC+8). Only when TZ is
     * unset/empty (a persisted/user TZ is kept); tzset() loads it into newlib's _timezone (read by
     * the console timestamp). This also seeds the first env var so `env` isn't empty on a fresh box. */
    {
        const char *tz = getenv("TZ");
        if ((tz == NULL) || (tz[0] == '\0'))
            setenv("TZ", "CST-8", 1);
    }
    tzset();
}

/* Start the shared interactive console: engine + the fs/env/time CLIs, then the platform + feature CLIs. */
void app_console_start(void)
{
    monitor_start();      /* shared engine: line editor + generic version/heap/tasks/echo */
    fs_cli_register();    /* ls/cat/rm/rmdir/mkdir/mv/cp/touch/hexdump/df/mkfs/chmod + hashes */
    env_cli_register();   /* setenv/getenv/env */
    time_cli_register();  /* date/uptime/sntp_update */
    app_console_register_platform();
}

/* The canonical pre-scheduler startup skeleton both platform main()s reduce to. Runs the platform
 * lifecycle hooks (weak no-ops above; strong-defined per platform) in canonical order, then starts the
 * scheduler. */
void app_startup_run(void)
{
    app_platform_boot_init();               /* chip + logger + banner + early SDK init (+ TF-M / flash-lock) */
    app_env_time_init();                    /* LittleFS mount + persisted env + default TZ (shared) */
    app_platform_rtc_seed();                /* seed wall clock from HW RTC (needs env loaded above) */
    app_console_start();                    /* shared console + app_console_register_platform (feature CLIs) */
    app_platform_wifi_bringup();            /* platform WiFi bring-up kickoff (AmebaD task / Bouffalo net_boot) */
    app_platform_pre_scheduler_services();  /* pre-scheduler service tasks (SNTP-non-GOTIP / NimBLE) */
    vTaskStartScheduler();
    for (;;) { }
}
#endif // !AT_PROJECT_QUIC && !AT_SDK_OWNS_MAIN

} // extern "C"

// AmebaD + Bouffalo own main(); Ameba-newer's SDK provides main() (its port exports app_pre_example
// instead), so gate on the capability flag AT_SDK_OWNS_MAIN -- not a chip macro.
#ifndef AT_SDK_OWNS_MAIN
extern "C" int main(void)
{
#ifdef AT_PROJECT_QUIC
    app_startup_run_headless();   // console-less self-test image (layout-sensitive)
#else
    app_startup_run();
#endif
    return 0;
}
#endif // !AT_SDK_OWNS_MAIN
