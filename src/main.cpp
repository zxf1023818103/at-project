// ===================================================================================
// Shared AT-firmware example entry point (every platform compiles this file). It carries:
//   - the shared, platform-neutral startup skeleton (app_startup_run) + main();
//   - the Bouffalo (BL602/BL616/...) and Ameba-newer (RTL8711/...) platform bodies, each behind its
//     own chip #ifdef (these were component/at_app_port; folded back here to drop a thin indirection).
// The AmebaD KM4 body is folded in here too (was component/amebad/app_port/amebad_app.cpp), behind
// its chip macros -- every platform body now lives in this one file. It is XIP-layout-sensitive
// (doc/amebad_xip_layout_boot_hang.md): boot-test amebad after layout-shifting edits.
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
// Same for AmebaD now that its body is folded in below (was its own TU, component/amebad/app_port).
#if defined(CONFIG_PLATFORM_8721D) || defined(CONFIG_PLATFORM_8710C)
#define AT_MAIN_AMEBAD 1
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

/* aws_get_mac moved to the shared component/aws_iot_config/aws_mac.c (BT MAC via
 * at_netif_bt_mac) — one Thing-name identity on every platform. */

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
#ifndef AT_PROJECT_AWS
    zip_cli_register();                 // zip/unzip (minizip over the VFS). Dropped in ANY AWS image
                                        // (link-level too, component/bouffalo_sdk/CMakeLists.txt), same
                                        // rule as amebad: connect-and-run image, heap headroom.
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
// body in the AmebaD section below.
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
// AmebaD (RTL8721D/8710C) KM4 platform body (was component/amebad/app_port/amebad_app.cpp,
// folded in here like the Bouffalo/Ameba-newer bodies). Strong overrides of the weak hooks
// below; the weak defaults are gated out for this TU via AT_MAIN_AMEBAD.
// NB: XIP-layout-sensitive (doc/amebad_xip_layout_boot_hang.md) — boot-test after moving code.
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
#include "at_log.h"               /* rxi log.c: log_info / log_error */
#include "log_lock.h"             /* log_lock_init */
#include "wifi_service.h"         /* shared WiFi provisioning + reconnect policy */
#include "wifi_port.h"            /* the platform HAL it drives (implemented below) */
#include "app_startup.h"          /* shared startup helpers (app_env_time_init / app_console_start / app_on_network_up) */
#ifndef AT_PROJECT_QUIC
#include "board_flash_lock.h"     /* cross-platform flash write-protect (flash_cli not linked in the QUIC image) */
#endif
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

#define WLAN_STA_IDX  0

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

/* WIFI_SECURITY string <-> rtw_security_t. A CCMP scan result is ambiguous (WPA2-AES and WPA3-SAE both
 * use CCMP; the scan buffer carries the cipher but NOT the AKM), so an AES AP maps to WPA2/WPA3 MIXED
 * and the RTL8721D supplicant negotiates the actual AKM per beacon (SAE for a WPA3/PMF-required AP, PSK
 * for a plain WPA2 AP). WPA2-AES forced OPEN auth and got the client deauth'd by a WPA3-only AP. */
static rtw_security_t wifi_sec_from_str( const char *s )
{
    if( s == NULL || s[ 0 ] == '\0' )            return RTW_SECURITY_UNKNOWN;
    if( strcmp( s, "OPEN" ) == 0 )               return RTW_SECURITY_OPEN;
    if( strcmp( s, "WEP" ) == 0 )                return RTW_SECURITY_WEP_PSK;
    if( strcmp( s, "WPA2_TKIP" ) == 0 )          return RTW_SECURITY_WPA2_TKIP_PSK;
    if( strcmp( s, "WPA2" ) == 0 || strcmp( s, "WPA2_AES" ) == 0 )
                                                 return RTW_SECURITY_WPA2_AES_PSK;
    if( strcmp( s, "WPA3" ) == 0 || strcmp( s, "WPA3_AES" ) == 0 )
                                                 return RTW_SECURITY_WPA3_AES_PSK;   /* SAE only */
    if( strcmp( s, "WPA2_WPA3" ) == 0 )          return RTW_SECURITY_WPA2_WPA3_MIXED; /* SAE or PSK */
    return RTW_SECURITY_UNKNOWN;
}
static const char *wifi_sec_to_str( rtw_security_t sec )
{
    switch( sec )
    {
        case RTW_SECURITY_OPEN:            return "OPEN";
        case RTW_SECURITY_WEP_PSK:         return "WEP";
        case RTW_SECURITY_WPA2_TKIP_PSK:   return "WPA2_TKIP";
        case RTW_SECURITY_WPA3_AES_PSK:    return "WPA3_AES";
        case RTW_SECURITY_WPA2_WPA3_MIXED: return "WPA2_WPA3";
        default:                           return "WPA2_AES";   /* WPA2-AES */
    }
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



/* Called by the IP task when the interface comes up (DHCP done) or goes down.
 * ipconfigIPv4_BACKWARD_COMPATIBLE=1 -> single-arg hook. */
/* The network-up/down body + all IPv6 logging/MLDv2 now live in component/freertos-plus-tcp_config/
 * at_netif.c (shared with bl602); this hook keeps only the AmebaD-specific dedup + driver signalling. */
extern "C" void at_netif_on_network_up( void );
extern "C" void at_netif_on_network_down( void );
extern "C" void at_netif_log_ipv6( const char * tag );
extern "C" int  at_netif_ipv6_global_set( void );
extern "C" void at_netif_ipv6_mld_start( void );

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
            if( at_netif_ipv6_global_set() && ( s_slaacLogged == 0 ) )
            {
                s_slaacLogged = 1;
                at_netif_log_ipv6( "SLAAC updated" );
            }
#endif
            return;
        }
        s_downLogged = 0;
        s_everUp = 1;
        /* Reliable "connected" cue (FOURWAY_HANDSHAKE_DONE is flaky on the driver's autoreconnect);
         * clears the monitor's wait without a redundant takeover. */
        wifi_link_set( 1 );
        /* Shared body: "[NET] ==== network UP ====", IPv6 (via at_netif_log_ipv6 -> our log_ipv6),
         * DHCP IP/mask/GW, the DHCP DNS list + DNS_SERVER/DNS_SERVER2 override, DHCP NTP, then
         * app_on_network_up() (SNTP + AWS/telemetry). Identical to bl602 now. */
        at_netif_on_network_up();
    }
    else
    {
        if( s_everUp && !s_downLogged )
        {
            s_downLogged = 1;
            at_netif_on_network_down();   /* "[NET] ==== network DOWN ====" + forget the DHCP NTP */
        }
        else
        {
            /* Boot DHCP re-kick (before a real up): still forget any stale NTP so the next network
             * re-derives it from that network's DHCP. */
            ulDHCPNtpServerAddress = 0U;
        }
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
            /* CCMP/AES: could be WPA2-PSK or WPA3-SAE (scan buffer has no AKM). Use MIXED so the
             * supplicant does SAE for a WPA3(/PMF-required) AP and PSK for a plain WPA2 AP - fixes the
             * WPA3 deauth-after-assoc (was hardcoded WPA2-AES -> OPEN auth -> WPA3 AP rejects). */
            else if( enc == RTW_ENCODE_ALG_CCMP ) fa->security = RTW_SECURITY_WPA2_WPA3_MIXED;
            else                                  fa->security = RTW_SECURITY_WPA2_WPA3_MIXED;
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

    /* Match the stock SDK's fATWC: for ANY PSK/SAE AP, pass RTW_SECURITY_WPA2_AES_PSK to wifi_connect
     * and let the driver's own connect-time scan read the AP's beacon RSN and auto-upgrade to WPA3/SAE
     * (this is what makes rtw_sae_prepare_commit fire). Forcing RTW_SECURITY_WPA3_AES_PSK ourselves makes
     * the driver mark auth alg=3 but skip the SAE commit build (empty commit -> AP never answers). SAE
     * itself is gated on the global wext_set_support_wpa3(ENABLE) set in wifi_set_user_config. */
    if( security & ( WPA_SECURITY | WPA2_SECURITY | WPA3_SECURITY ) )
        security = RTW_SECURITY_WPA2_AES_PSK;

    /* Broadcast deauth before each attempt flushes the AP's stale station entry for this MAC (the
     * driver-only chip never deauthed on reset), so the assoc is accepted on the first try; the 8-try
     * loop covers the transient status-30 reject. pscan restricts the connect probe to `chan`.
     *
     * status-30 comeback trap: on a PMF AP the unprotected deauth is ignored, the stale entry makes
     * the first assoc answer status 30 ("denied temporarily"), and the driver's immediate comeback
     * assoc SUCCEEDS -- but leaves the driver's RX path dead (no DHCP OFFER, no RA; TX still works).
     * So "associated" alone is not success: after each associate, VERIFY a DHCP lease arrives; if it
     * doesn't, disconnect and go around the loop (the comeback already cleared the AP's stale state,
     * so the next associate is clean and DHCP completes normally). */
    for( attempt = 1; attempt <= 8; attempt++ )
    {
        u8 c = chan, ps = PSCAN_ENABLE;
        int waited;
        rltk_wlan_tx_deauth( 1, 0 );
        vTaskDelay( pdMS_TO_TICKS( 80 ) );
        if( chan != 0 ) { wifi_set_pscan_chan( &c, &ps, 1 ); }
        if( wifi_connect( (char *) ssid, security, (char *) pass,
                          (int) strlen( ssid ), (int) strlen( pass ), 0, NULL ) != RTW_SUCCESS )
        {
            log_error( "[KM4] wifi_connect attempt %d FAILED, retrying...", attempt );
            vTaskDelay( pdMS_TO_TICKS( 800 ) );
            continue;
        }

        /* Post-associate (every connect): full power-save off + clear the reassoc hold so the
         * re-DHCP's genuine network-up is honoured, then re-kick DHCP (fresh DISCOVER). */
        wifi_set_power_mode( 0, 0 );
        wifi_disable_powersave();
        wifi_reassoc_set( 0 );
        { int rssi = 0; wifi_get_rssi( &rssi ); log_info( "[KM4] associated; power-save off; RSSI %d dBm", rssi ); }
        FreeRTOS_NetworkDown( &xInterface );

        /* Lease verification (the status-30 comeback detector): up to 12 s for a non-zero IPv4. */
        for( waited = 0; waited < 120; waited++ )
        {
            uint32_t ulIP = 0, ulMask, ulGW, ulDNS;
            FreeRTOS_GetAddressConfiguration( &ulIP, &ulMask, &ulGW, &ulDNS );
            if( ulIP != 0U ) { ok = 1; break; }
            vTaskDelay( pdMS_TO_TICKS( 100 ) );
        }
        if( ok ) { break; }

        log_error( "[KM4] associated but no DHCP lease in 12s (status-30 comeback / dead RX) - re-associating..." );
        wifi_disconnect();
        vTaskDelay( pdMS_TO_TICKS( 1500 ) );   /* let the AP-side state settle before the clean retry */
    }
    if( !ok ) return -1;

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

    /* Write-protect the firmware region (bottom 3 MB: boot + both OTA slots) against stray writes,
     * matching bl602's boot-time lock (its main() calls at::lockFirmwareFlash). The top 1 MB (LittleFS
     * + TF-M ITS) stays writable, so env/KV, secure storage and the LFS self-test below are unaffected;
     * an OTA unlocks/relocks around its slot write (ota_flash.c). Done here (post-IPC) so
     * FLASH_Write_Lock's KM0 park is ready, and nothing writes the bottom 3 MB before the first OTA.
     * Gated with the console (flash_cli + monitor_port supply the flash lock); the QUIC image has
     * neither the console nor OTA. board_flash_log_status() prints the resulting lock state. */
#ifndef AT_PROJECT_QUIC
    board_flash_lock_fw();
    board_flash_log_status();
#endif

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

    /* NOTE: do NOT call the ROM crypto-engine init (CRYPTO_Init / 0x1010ea49 NS alias) here. In a
     * no-TF-M image the SAU isn't configured for those NS ROM crypto aliases (see
     * reference_amebad_hw_crypto), so the call itself bus-faults. WPA3/SAE doesn't need the IPSEC
     * engine -- its ECC/bignum math is pure software (ROM mbedTLS 2.4.0, use_hw_crypto_func=0). */
#if defined(AMEBAD_ROM_KAT)
    /* ROM-mbedTLS image: wire the ROM mbedTLS ram-map calloc/free BEFORE WiFi. lib_wlan.a's WPA3/SAE
     * ECC (ROM mbedtls_ecp_*) allocates during the connect handshake via this ram-map; wiring it here
     * (rather than the post-associate KAT) is what lets the SAE commit compute cleanly. */
    { extern void rom_mbedtls_init( void ); rom_mbedtls_init(); }
#endif

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
    /* Periodic MLDv2 reports (now in at_netif) so the router's MLD-snooping bridge forwards the
     * solicited-node NS / the router's RAs to us - otherwise inbound IPv6 is unreachable. */
    at_netif_ipv6_mld_start();
#endif
    log_info( "[KM4] FreeRTOS_IPInit_Multi..." );
    FreeRTOS_IPInit_Multi();
    amebad_freertos_tcp_set_ready();

#ifdef AT_PROJECT_VFS_SELFTEST
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
    amebad_monitor_cmds_register();     /* AmebaD port cmds: reboot / dw / ew / flashdbg / efuse / >> / km0 / km4 */
    flash_cli_register();               /* shared `flash` lock/unlock/status (component/flash_cli_config) */
    { extern void wifi_cli_register(void); wifi_cli_register(); }               /* shared `wifi` forget/drop */
    { extern void amebad_wifi_cli_register(void); amebad_wifi_cli_register(); } /* AmebaD-only `iwpriv` */
    { extern void net_cli_register(void); net_cli_register(); }                 /* shared `resolve` / `ip6` */
#ifdef AT_PROJECT_IPERF
    { extern void iperf_cli_register(void); iperf_cli_register(); }              /* shared `iperf` server/tx/udp */
#endif
#ifdef AT_PROJECT_NIMBLE
    { extern void ble_tp_cli_register(void); ble_tp_cli_register(); }            /* shared `bletp` BLE throughput test */
#endif
#ifdef AMEBAD_ENABLE_MINIZIP
    { extern void zip_cli_register(void);  zip_cli_register();  }   /* zip/unzip demo (non-AWS images) */
#endif
#ifdef AMEBAD_SBOOT
    { extern void sboot_cli_register(void); sboot_cli_register(); } /* secure-boot Ed25519 self-test */
#endif
#ifdef AT_PROJECT_CRT_BUNDLE
    { extern void crt_bundle_cli_register(void); crt_bundle_cli_register(); } /* compressed CA root bundle */
#endif
#ifdef AT_CRYPTO_SELFTESTS
    /* Diagnostic image (make CRYPTO_SELFTESTS=ON): the Mbed TLS crypto self-test CONSOLE COMMANDS,
     * same umbrella + form as bl602 (PKCS11=ON / BENCH=ON keep the boot-auto-run diagnostic form). */
    { extern void bench_cli_register(void); bench_cli_register(); }              /* Mbed TLS microbenchmark */
    { extern void crt_bundle_cli_register(void); crt_bundle_cli_register(); }    /* CA bundle info/verify */
    { extern void pkcs11_cli_register(void); pkcs11_cli_register(); }            /* corePKCS11 self-test */
#endif
#ifdef AMEBAD_ENABLE_WPS
    { extern void wps_cli_register(void); wps_cli_register(); }     /* WPS (PBC/PIN) connect test */
#endif
#if defined(AMEBAD_EFUSE_DEBUG) && defined(AMEBAD_TFM_STORAGE)
    { extern void efuse_burn_cli_register(void); efuse_burn_cli_register(); } /* DEBUG eFuse burn (provisioning) */
#endif
#ifdef AT_PROJECT_AWS
    /* Any AWS image (lean telemetry or full agent): shared `aws` provisioning command
     * (component/aws_iot_config) + AmebaD TF-M PSA diagnostics (pstest/xiptest) + `ota_http <url>`.
     * aws_provision_init() is a shared weak no-op. */
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
    /* Full AWS IoT agent (AWSOTA=ON): coreMQTT-Agent + telemetry + OTA over mutual TLS
     * (endpoint+cert from LittleFS, key from TF-M Protected Storage). */
    extern void aws_iot_demo_start(void);
    aws_iot_demo_start();
#elif defined(AT_PROJECT_AWS)
    /* Lean AWS image (AWS=ON alone): the telemetry-only client, exactly like bl602's AWS=ON. */
    extern void aws_telemetry_start(void);
    aws_telemetry_start();
#endif
    /* iperf: manual `iperf server|tx|udp` console command (registered above when linked), same as
     * bl602 -- no auto-start at network-up. */
#endif /* AT_PROJECT_QUIC */
}

/* ---- shared example entry seams (component/app_startup_config): strong AmebaD overrides ----------
 * app_startup_run() (the shared skeleton) calls these in canonical order. Only the parts that differ
 * per platform live here; the common middle (env/time + console) is in the shared helper. AmebaD does
 * its flash-lock + rtc-seed inside wifi_bringup_thread (not pre-scheduler), so those hooks stay weak
 * no-ops for this platform (app_platform_pre_scheduler_services too - AmebaD starts services on
 * network-up). We're inside the amebad `extern "C"` block, so these keep C linkage. */

/* Chip + logger + banner + (optional) TF-M Non-Secure init and PSA/storage selftests. Runs first. */
void app_platform_boot_init(void)
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
}

/* WiFi+DHCP bring-up runs as its own task (wifi_connect blocks). */
void app_platform_wifi_bringup(void)
{
    if (xTaskCreate(wifi_bringup_thread, "wifi_bringup", 4096, NULL,
                    tskIDLE_PRIORITY + 3, NULL) != pdPASS)
        log_error("[KM4] xTaskCreate(wifi_bringup) failed");
}

/* Hooks AmebaD doesn't use: no-op strong defs (the shared weak defaults are gated out of this TU
 * now that the AmebaD body shares it -- see AT_MAIN_AMEBAD). RTC seeding happens inline in
 * wifi_bringup_thread; there are no pre-scheduler services (the WiFi thread starts everything). */
void app_platform_rtc_seed(void) { }
void app_platform_pre_scheduler_services(void) { }

}  // extern "C"  (closes the linkage spec opened above; main() gets its own in src/main.cpp)

#endif // AmebaD (CONFIG_PLATFORM_8721D / CONFIG_PLATFORM_8710C)

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

/* Weak no-op hook defaults. Compiled EXCEPT on Bouffalo and AmebaD, whose strong overrides share
 * this TU (a weak+strong pair in one TU is a redefinition). Ameba-newer keeps the weak defaults. */
#if !defined(AT_MAIN_BOUFFALO) && !defined(AT_MAIN_AMEBAD)
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
