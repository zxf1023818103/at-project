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
#include "dot11d.h"        // 802.11d country auto-detect (component/80211d_config; ports per platform)

#include <stdio.h>    /* printf */
#include <stdlib.h>   /* getenv / setenv (default TZ) */
#include <time.h>     /* tzset */

// True on the Bouffalo family (any BL chip). Used to gate the shared weak hook defaults out of the
// Bouffalo compile: Bouffalo's STRONG hook overrides are in THIS same TU (folded in below), so a weak
// default beside them would be a duplicate definition. AmebaD/Ameba-newer keep the weak defaults (their
// strong overrides, if any, are in a different TU -> normal weak/strong link resolution).
#if defined(BL602) || defined(BL702) || defined(BL606P) || defined(BL808) || defined(BL616) || defined(BL616CL) || defined(BL618DG)
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
#include "BoardFlashLock.h"
#include "board_flash_lock.h"   /* cross-platform flash write-protect: board_flash_lock_fw / _log_status / flash_cli_register */
#include "wifi_service.h"   /* shared WiFi provisioning + reconnect policy */
#include "wifi_port.h"      /* the platform HAL it drives (implemented below) */
#include "FreeRTOS_IP.h"    /* vApplicationIPNetworkEventHook / eNetworkUp / FreeRTOS_GetAddressConfiguration */
#endif

extern "C" {

__attribute__((__weak__)) void *__dso_handle = (void *)&__dso_handle;

#if defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2
/* Strong override of the Ameba-newer SDK's __weak wifi_do_fast_connect (wifi_fast_connect.c calls it
 * via p_wifi_do_fast_connect at boot): a no-op, so the SDK's flash-blob fast-connect stays disabled --
 * the shared wifi_service (env credentials + cached channel) owns provisioning. It used to spawn the
 * C++ TestTask here, but TestTask::routine() has been all-commented-out for ages (an empty 12 KB-stack
 * task); the spawn and the AtTask/TestTask includes are gone. */
int wifi_do_fast_connect(void) {
    return 0;
}
#endif


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

#if defined BL606P || defined BL808 || defined BL616 || defined BL616CL || defined BL618DG
extern void wifi_main(void *param);
#ifdef CONFIG_LPAPP
extern "C" int bl_lp_init(void);              /* bflb_sys/bl616_lp.c — lp subsystem bring-up (LP_FW=ON) */
extern "C" int app_clock_init(void);          /* power_mgmt/clock_manager.c — F32K/RC32K bring-up for PDS wake */
/* HBN_Enable_RTC_Counter() comes from bl616_hbn.h (already visible); pm_rc32k_auto_cal_init() from bl616_pm.h. */
extern "C" void pm_rc32k_auto_cal_init(void); /* bl616_std — init RC32K auto-cal state (mtimer/rtc counters) */
extern "C" void lp_recovery_register(void);   /* power_mgmt/lp_recovery.c — post-PDS15-wake board restore */
extern "C" void hal_pm_ldo11_cfg(uint8_t soc_v, uint8_t rt_v, uint8_t aon_v); /* bl616_std — PDS LDO levels */
#include "bflb_gpio.h"   /* JTAG pin mux for CKLink low-power debugging (LP builds) */
#if defined(AT_PROJECT_NIMBLE)
extern "C" void tickless_ble_hooks_init(void); /* power_mgmt/tickless_ble.c — BLE PDS15 sleep arbitration */
#endif

/* app_clock_init() brings up the 32 kHz clock (RC32K auto-cal + optional xtal32k) that drives the HBN
 * RTC, so a PDS15 sleep started via `lpsleep` self-wakes on its RTC compare. It BLOCKS on a semaphore
 * (waits for its cal tasks), so it must run AFTER the scheduler starts — hence a short-lived task, not
 * an inline call from the pre-scheduler app_platform_boot_init path (which would ecall-yield with no
 * scheduler and trap). Mirrors wl_lp's f32k_clk_init_task. */
static void lp_clock_init_task(void *arg) {
    (void)arg;
    app_clock_init();
    vTaskDelete(NULL);
}
#endif

static int hal_wifi_start_firmware_task(void) {
    /* WiFi clock ungate + WiFi AHB reset were already done once in board_init (board.c
     * system_clock_init, CONFIG_WIFI6). */

    /* Enable wifi irq */
    extern void interrupt0_handler(void);
    bflb_irq_attach(WIFI_IRQn, (irq_callback)interrupt0_handler, NULL);
    bflb_irq_enable(WIFI_IRQn);

    /* Create the macsw firmware task via the SDK's own wifi_task_create() (macsw_plat.c) rather than a
     * bare xTaskCreate(wifi_main, ...). wifi_task_create() stores the handle in macsw's private
     * `wifi_task_handle`, which the WiFi ISR path uses (wifi_task_resume -> vTaskNotifyGiveFromISR) to
     * wake wifi_main. A hand-rolled xTaskCreate leaves that handle NULL, so the MAC-hardware IRQ can
     * never wake the FW task -> its MAC bring-up blocks forever (macsw prints "features/mode/dma" then
     * stalls; the whole device idles) and every later wifi_mgmr kmsg hits a NULL command queue. It also
     * uses the correct macsw priority (27) and stack (1324). */
#ifdef CONFIG_LPAPP
    /* Low-power WiFi (make ... LP_FW=ON): bring the lp subsystem up BEFORE macsw starts. CONFIG_LPAPP
     * compiles macsw in power-save mode (__macsw_conf_bl_wifi_ps_enable=1), so its FW task calls the
     * bl_lp_* hooks (macsw_plat.c) during normal WiFi — they dereference gp_lp_env and
     * iot2lp_para->wifi_parameter, which are NULL until bl_lp_init() runs (bl_lp_env_init allocates
     * gp_lp_env; bl_lp_fw_init validates the concatenated lp_fw blob and populates iot2lp_para). Without
     * this the very first connect faults in bl_lp_get_resume_wifi. */
    bl_lp_init();
    /* Post-PDS15-wake board restore (bflb_power_mgmt/lp_recovery.c): the SDK contract for the
     * minimal tickless.c is that the app registers the bl_lp sys-exit callback (board_recovery +
     * FreeRTOS tick re-arm + console RX IRQ), like every SDK lp demo's lp_exit. Without it the
     * first real resume comes back to a dead board. */
    lp_recovery_register();
    /* PDS LDO levels for the sleep window (SOC/RT/AON = the SDK lp demos' PM_PDS_LDO_LEVEL_*_DEFAULT
     * = 10 = 1.00V, no-DCDC board). Every vendor lp demo sets this in main() before any PDS15 sleep;
     * without it the retention supply during PDS is whatever the ROM left. */
    hal_pm_ldo11_cfg(10, 10, 10);
    /* Prerequisites for the HBN RTC to actually count (wl_lp does these in main() before its f32k task):
     * start the RTC counter and seed the RC32K auto-cal state. Without HBN_Enable_RTC_Counter() the RTC
     * stays frozen, so app_clock_init's RC32K calibration reads rtc_us=0 forever (never converges) and a
     * PDS15 sleep never wakes. Plain register writes — safe here in the pre-scheduler path. */
    HBN_Enable_RTC_Counter();
    pm_rc32k_auto_cal_init();
    /* Bring up the HBN 32 kHz clock for PDS-wake, from a task that runs once the scheduler is live
     * (app_clock_init blocks — see lp_clock_init_task). Queued here pre-scheduler; runs later. */
    xTaskCreate(lp_clock_init_task, (char *)"lp_clk", 1024, NULL, 12, NULL);
#if defined(AT_PROJECT_NIMBLE)
    /* BLE tickless arbitration (bflb_power_mgmt/tickless_ble.c): cap each PDS15 sleep at the BLE
     * controller's next LL event and restore it on wake, so `tickless <dtim>` keeps ADV/connections
     * alive. Just fills the hook struct — inert until btble_controller_init() runs. */
    tickless_ble_hooks_init();
#endif
#endif
    extern void wifi_task_create(void);
    wifi_task_create();

    return 0;
}
#endif // defined BL606P || defined BL808 || defined BL616 || defined BL616CL || defined BL618DG

#if defined BL616 || defined BL616CL || defined BL618DG
// FreeRTOS-Plus-TCP bring-up (component/bouffalo_sdk/freertos_tcp_port): register the
// STA interface + DHCP endpoint + start the IP task. Called once the WiFi driver is up.
extern "C" void bouffalo_tcp_bringup(void);

// fhost's app-facing event fan-out is the shared async_event bus: async_register_event_filter(EV_WIFI,
// cb, priv) with cb(async_input_event_t ev, void*), ev->code carrying the CODE_WIFI_ON_* event
// (mirrors the SDK's examples/wifi/sta/wifi_tcp/main.c). We register bl616_wifi_event_cb, which widens
// to the shared wifi_event_handler(uint32_t,uint32_t).
extern "C" {
#include "async_event.h"      // async_input_event_t / async_register_event_filter / EV_WIFI
}
static void bl616_scan_done(void);            // wakes wifi_port_scan  (CODE_WIFI_ON_SCAN_DONE)
static void bl616_wifi_event_cb(async_input_event_t ev, void *priv) {
    (void) priv;
    void wifi_event_handler(uint32_t code, uint32_t code2);   // defined below
    wifi_event_handler((uint32_t) ev->code, (uint32_t) ev->value);
}
#endif // defined BL616 || defined BL616CL || defined BL618DG

#if defined BL602 || defined BL702 || defined BL616 || defined BL616CL || defined BL618DG
static void bouffalo_conn_event(int connected);   // connect result (CONNECTED/DISCONNECT) -> wifi_port_connect
#endif

#if defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616 || defined BL616CL || defined BL618DG
void wifi_event_handler(uint32_t code, uint32_t code2) {
    (void) code2;
    switch (code) {
        case CODE_WIFI_ON_INIT_DONE: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_INIT_DONE\n", __func__);
            // Regulatory domain: the persisted WIFI_COUNTRY (set by the user or by
            // dot11d_autodetect_country's majority vote) wins; "CN" is only the first-boot default.
            const char *cc = getenv("WIFI_COUNTRY");
            if (!dot11d_cc_valid(cc)) cc = NULL;   // garbage/empty persisted value -> default (dot11d_cc_valid: component/80211d_config)
#if defined BL602 || defined BL702
            wifi_conf_t conf = { .country_code = "CN" };
            if (cc != NULL) { conf.country_code[0] = cc[0]; conf.country_code[1] = cc[1]; conf.country_code[2] = '\0'; }
            wifi_mgmr_start_background(&conf);
#endif // defined BL602 || defined BL702
#if defined BL606P || defined BL808
            (void)cc;
#endif
#if defined BL616 || defined BL616CL || defined BL618DG
            // fhost bootstrap: fhost_init() (from on_tcpip_init_done) fires INIT_DONE; the wifi_mgmr
            // task is started HERE in response (mirrors examples/wifi/sta/wifi_tcp/main.c's
            // wifi_event_handler), which then dispatches MGMR_DONE. Country code is applied later from
            // wifi_port_scan() (MAC guaranteed ready), same as before.
            // Declared in wifi_mgmr.h (returns int) — do NOT re-declare it here:
            // on bl618dg that header is in scope and a `void` redeclaration is a
            // hard C++ error ("conflicting declaration").
            (void)wifi_mgmr_task_start();
            (void)cc;
#endif // defined BL616 || defined BL616CL || defined BL618DG
            break;
        }
        case CODE_WIFI_ON_MGMR_DONE: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_MGMR_DONE\n", __func__);
#if defined BL616 || defined BL616CL || defined BL618DG
            // Bring up FreeRTOS-Plus-TCP now (driver is up, MAC is valid): register
            // the STA interface + DHCP endpoint + start the IP task. The stack then
            // waits on phy-link (wifi_mgmr_sta_state_get) before sending DHCP, so it
            // is safe to init before association.
            bouffalo_tcp_bringup();
#endif // defined BL616 || defined BL616CL || defined BL618DG
            // Start the shared WiFi provisioning + reconnect service (component/wifi_service_config).
            // It runs in its own task (a blocking connect must not run here: wifi_event_handler runs
            // with the scheduler suspended / in the async-event context, and the drivers' connect
            // calls block). It reads WIFI_SSID/WIFI_PASSWORD from env, connects via the cached
            // channel or a scan, and supervises the link -- driving the chip's wifi_port_* HAL
            // below. Same service AmebaD uses.
            wifi_service_start();
            break;
        }
        case CODE_WIFI_ON_SCAN_DONE: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_SCAN_DONE\n", __func__);
#if defined BL616 || defined BL616CL || defined BL618DG
            bl616_scan_done();   // wake wifi_port_scan: results are ready
#endif
            break;
        }
        case CODE_WIFI_ON_CONNECTING: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_CONNECTING\n", __func__);
            break;
        }
        case CODE_WIFI_ON_CONNECTED: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_CONNECTED\n", __func__);
#if defined BL602 || defined BL702 || defined BL616 || defined BL616CL || defined BL618DG
            bouffalo_conn_event(1);   // wake wifi_port_connect: association succeeded
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
#if defined BL602 || defined BL702 || defined BL616 || defined BL616CL || defined BL618DG
            bouffalo_conn_event(0);   // wake wifi_port_connect: association failed/dropped
            wifi_service_on_event(WIFI_SVC_EV_DISCONNECT);   // tell wsvc to re-associate
#endif
#ifdef CONFIG_LPAPP
            /* Safety: if `tickless` armed the idle-PDS15 path, disarm it on WiFi loss. The disconnected
             * sleep branch has no DTIM wake and PDS15 doesn't wake on UART, so an un-connected auto-sleep
             * would swallow console/AT input until the RTC backup fires. */
            { extern int tickless_exit(void); tickless_exit(); }
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

#if defined BL602 || defined BL702 || defined BL616 || defined BL616CL || defined BL618DG
/* Network-up trigger, unified with AmebaD: the FreeRTOS+TCP IP task fires eNetworkUp when DHCP
 * completes (bl602_tcp_bringup / bouffalo_tcp_bringup -> at_netif_bringup -> FreeRTOS_IPInit_Multi).
 * This is the single network-up cue -- it replaces the SDK's CODE_WIFI_ON_GOT_IP -> app_on_network_up().
 * The shared weak hook (at_netif.c) only prints; it does NOT call at_netif_on_network_up(), so bl616
 * needs this strong override too. On bl616 at_netif_signal_got_ip() is a safe no-op (bouffalo_ops.on_got_ip
 * is NULL, guarded in at_netif.c) -- wl80211 uses use_dhcp=0 so the driver never waits on its own got-IP.
 * The IPv6 link-local endpoint + the DHCP give-up fallback both fire this with a 0.0.0.0 lease, and the
 * IPv6 global (SLAAC) endpoint re-fires it once up, so guard on a non-zero lease and dedup per up/down. */
extern void at_netif_signal_got_ip( uint32_t );   /* drive bl602 wifi_mgmr out of ipObtaining (no-op on bl616) */
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
#if defined BL616 || defined BL616CL || defined BL618DG
    // fhost bootstrap (mirrors examples/wifi/sta/wifi_tcp/main.c's wifi_start_firmware_task):
    // fhost_init() brings up the wifi6 full-host stack on the wifi_main task (created pre-scheduler in
    // hal_wifi_start_firmware_task via wifi_task_create) and fans out CODE_WIFI_ON_INIT_DONE to the app
    // handler (registered just below via async_register_event_filter). The INIT_DONE handler then calls
    // wifi_mgmr_task_start() -> MGMR_DONE. Replaces the old wl80211_init()+wifi_mgmr_init() pair.
    { extern int fhost_init(void); fhost_init(); }
#endif // defined BL616 || defined BL616CL || defined BL618DG
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

#if defined BL602 || defined BL702 || defined BL616 || defined BL616CL || defined BL618DG
// ---- connect-result signal (shared by the bl602/wifi4 and bl616/fhost HAL blocks below): the
//      drivers' connect calls are async, so wifi_port_connect blocks until the driver's
//      CODE_WIFI_ON_CONNECTED (ok) / CODE_WIFI_ON_DISCONNECT (fail) event (fed from
//      wifi_event_handler) wakes it. Notify-before-wait is race-safe (xTaskNotifyGive latches). ----
static TaskHandle_t s_bouffalo_conn_waiter;
static volatile int s_bouffalo_conn_ok;
static void bouffalo_conn_event(int connected) {
    TaskHandle_t t = s_bouffalo_conn_waiter;
    if (t != NULL) { s_bouffalo_conn_ok = connected; s_bouffalo_conn_waiter = NULL; xTaskNotifyGive(t); }
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
    dot11d_autodetect_country();   // component/80211d_config: once, when WIFI_COUNTRY is unset (before associating)
    s_bouffalo_conn_ok = 0;
    s_bouffalo_conn_waiter = xTaskGetCurrentTaskHandle();
    // A non-zero chan makes the wifi4 connect probe just that one channel for the BSSID.
    wifi_mgmr_sta_connect(NULL, (char *)ssid, (char *)pass, NULL, NULL, 0, chan);
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(6000)) == 0) {
        s_bouffalo_conn_waiter = NULL;   // timeout -> failure
        return -1;
    }
    return s_bouffalo_conn_ok ? 0 : -1;
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

// ===================================================================================
// bl616 WiFi HAL (wifi_port.h): the wl80211 mechanics behind the shared wifi_service policy.
// Mirrors the bl602 block above, but over wl80211's params-struct API -- which is source-incompatible
// with wifi4 (wifi_mgmr_sta_connect takes ONE struct; there is no wifi_mgmr_sta_enable, the STA MAC
// comes from efuse via platform_get_mac). Scan/connect are async: the driver's CODE_WIFI_ON_* events
// (routed through bl616_wifi_event_cb -> wifi_event_handler) wake these blocking calls, which run in
// the wifi_service task. Notify-before-wait is race-safe (xTaskNotifyGive latches).
// ===================================================================================
#if defined BL616 || defined BL616CL || defined BL618DG

static TaskHandle_t s_bl616_scan_waiter;
static void bl616_scan_done(void) {
    TaskHandle_t t = s_bl616_scan_waiter;
    if (t) { s_bl616_scan_waiter = NULL; xTaskNotifyGive(t); }
}

/* wifi_mgmr_scan_ap_all() calls this for each AP in the result tree; take the channel of the first
 * exact-SSID match. */
struct bl616_scan_ctx { const char *ssid; int found; uint8_t chan; int seen; };
static void bl616_scan_match_cb(void *env, void *arg, wifi_mgmr_scan_item_t *it) {
    (void)arg;
    struct bl616_scan_ctx *c = (struct bl616_scan_ctx *)env;
    c->seen++;
    if (!c->found && it->ssid_len == (int)strlen(c->ssid) &&
        memcmp(c->ssid, it->ssid, (size_t)it->ssid_len) == 0) {
        c->found = 1;
        c->chan  = it->channel;
        printf("[SCAN] match '%.*s': ch=%u auth=%u cipher=%u rssi=%d\r\n",
               (int)it->ssid_len, it->ssid, (unsigned)it->channel,
               (unsigned)it->auth, (unsigned)it->cipher, (int)it->rssi);
    }
}

/* Diagnostic for the "not found in scan" case: without it a missing AP and a
 * deaf receiver look identical in the log. Lists what the scan DID see. */
static void bl616_scan_dump_cb(void *env, void *arg, wifi_mgmr_scan_item_t *it) {
    (void)env; (void)arg;
    printf("[SCAN]   ch=%-3u rssi=%-4d auth=%u cipher=%u %.*s\r\n",
           (unsigned)it->channel, (int)it->rssi, (unsigned)it->auth,
           (unsigned)it->cipher, (int)it->ssid_len, it->ssid);
}

extern "C" {

unsigned wifi_port_task_stack_words(void) { return 1536; }   // wl80211 connect drives the supplicant

int wifi_port_enable(void) {
    return 0;   // no wifi_mgmr_sta_enable on wl80211; the STA MAC is read from efuse (platform_get_mac)
}

void wifi_port_autoreconnect_disable(void) {
    wifi_mgmr_sta_autoconnect_disable();   // wifi_service is the single reconnect authority
}

int wifi_port_scan(const char *ssid, uint8_t *chan, int *sec) {
    // Regulatory domain, deferred from CODE_WIFI_ON_INIT_DONE (where the MAC kmsg queue is still NULL ->
    // configASSERT abort). wsvc only reaches here after MGMR_DONE + the WiFi FW task is up, so the MAC is
    // ready for wifi_mgmr_set_country_code's kmsg round-trip. Apply the persisted WIFI_COUNTRY override
    // once; "CN" (already seeded from efuse by rfparam_init) is the fallback.
    static bool s_cc_set = false;
    if (!s_cc_set) {
        s_cc_set = true;
        const char *cc = getenv("WIFI_COUNTRY");
        if (!dot11d_cc_valid(cc)) cc = NULL;
        wifi_mgmr_set_country_code((char *)((cc != NULL) ? cc : "CN"));
    }
    wifi_mgmr_scan_params_t p;
    memset(&p, 0, sizeof(p));
    /* UNDIRECTED active scan (ssid_length stays 0 = scan everything), then match
     * in the result tree. A directed scan is faster, but it makes "AP absent"
     * and "receiver deaf" indistinguishable — both yield an empty result — and
     * re-scanning undirected afterwards is rejected (rc=-1) while the first scan
     * is still retiring. Scanning everything once gives both the match and a
     * usable diagnostic. Mirrors the SDK's own wifiprov_bl616.c (all-zero params). */
    s_bl616_scan_waiter = xTaskGetCurrentTaskHandle();
    if (wifi_mgmr_sta_scan(&p) != 0) { s_bl616_scan_waiter = NULL; return -1; }
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(4000));   // woken by CODE_WIFI_ON_SCAN_DONE
    struct bl616_scan_ctx ctx = { ssid, 0, 0, 0 };
    wifi_mgmr_scan_ap_all(&ctx, NULL, bl616_scan_match_cb);
    printf("[SCAN] saw %d AP(s), '%s' %s\r\n", ctx.seen, ssid, ctx.found ? "FOUND" : "absent");
    if (!ctx.found) {
        wifi_mgmr_scan_ap_all(NULL, NULL, bl616_scan_dump_cb);   // list what IS on the air
        return -1;                          // SSID not present
    }
    if (chan) *chan = ctx.chan;
    if (sec)  *sec  = -1;                   // wl80211 derives security from the beacon (no cache)
    return 0;
}

int wifi_port_connect(const char *ssid, const char *pass, uint8_t chan, int sec) {
    (void)chan; (void)sec;                  // freq1=0 -> driver does its own directed scan+connect;
                                            // security is beacon-derived
    // Use fhost's convenience connector (same one the SDK CLI `wifi_sta_connect` uses); it assembles the
    // wifi_mgmr_sta_connect_params_t (ssid/key + lengths, akm) correctly. AKM: WPA2 = "RSN" (the valid
    // set is WPA/RSN/SAE). use_dhcp=0 -- FreeRTOS+TCP owns DHCP (use_dhcp=1 arms the driver's ~15s
    // self-disconnect on DHCP timeout). Async: CODE_WIFI_ON_CONNECTED/_DISCONNECT wakes the waiter below.
    extern int wifi_sta_connect(const char *ssid, const char *key, const char *bssid,
                                const char *akm_str, uint8_t pmf_cfg, uint16_t freq1, uint16_t freq2,
                                uint8_t use_dhcp);
    const char *akm = NULL;   // wifi_sta_connect scans internally -> auto-detects AKM from the beacon
    /* pmf_cfg maps straight onto the supplicant's `ieee80211w` (enum mfp_options: 0=disabled,
     * 1=optional, 2=required). It MUST NOT be 0: WPA3 mandates protected management frames, so
     * an SAE AP advertises MFPR and wpa_supplicant's BSS match rejects it outright when the
     * network has ieee80211w=0 — the symptom is CTRL-EVENT-BSS-ADDED immediately followed by
     * CTRL-EVENT-NETWORK-NOT-FOUND ("SSID error, scan no bssid and channel"), i.e. the BSS was
     * seen and then discarded. 1 (optional) is the value that works for both: a WPA2 AP that
     * does not ask for MFP is unaffected, a WPA3 AP gets MFPC=1 and satisfies its MFPR. */
    s_bouffalo_conn_ok = 0;
    s_bouffalo_conn_waiter = xTaskGetCurrentTaskHandle();
    if (wifi_sta_connect(ssid, pass, NULL, akm, 1, 0, 0, 0) != 0) { s_bouffalo_conn_waiter = NULL; return -1; }
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(12000)) == 0) { s_bouffalo_conn_waiter = NULL; return -1; }
    /* Pin the TX rate to legacy 11g 24M (no HT/AMPDU path): the macsw MAC core wedges its
     * TX AC queues under bidirectional TCP on auto rate (internal frame left:N + hal_machw
     * asserts; reproduced with the stock SDK example too). Called HERE (wifi service task)
     * and not in the CONNECTED event callback — that runs in the async-event Timer context
     * where the blocking kmsg round-trip inside wifi_mgmr_rate_config trips configASSERT. */
    if( s_bouffalo_conn_ok )
    {
        struct wifi_mgmr_rate_config rc = { 12 /* WIFI_RC_IDX_24M */, 8 /* WIFI_RC_IDX_6M */ };
        ( void ) wifi_mgmr_rate_config( &rc );
    }
    return s_bouffalo_conn_ok ? 0 : -1;        // set by CODE_WIFI_ON_CONNECTED / _DISCONNECT
}

void wifi_port_disconnect(void) { extern int wifi_sta_disconnect(void); wifi_sta_disconnect(); }   // fhost: wifi_sta_disconnect (not wifi_mgmr_sta_disconnect)

int          wifi_port_sec_from_str(const char *s) { (void)s;   return -1; }
const char * wifi_port_sec_to_str(int sec)         { (void)sec; return ""; }

}  // extern "C"
#endif // defined BL616 || defined BL616CL || defined BL618DG

/* ---- shared-startup platform seams: strong overrides for the Bouffalo platform -------------------
 * Strong overrides of the weak no-ops below. We're inside the outer `extern "C"` block, so these keep
 * C linkage and override the weak C symbols. app_console_start() calls the first; app_on_network_up()
 * (from the GOT_IP handler) calls the second. */
void app_console_register_platform(void)
{
    bouffalo_monitor_cmds_register();   // reboot
    flash_cli_register();               // shared `flash` lock/unlock/status (component/flash_cli_config)
    { extern void dsleep_cli_register(void); dsleep_cli_register(); }   // shared `dsleep [uart] [rtc <s>]` (component/dsleep_config)
#ifdef CONFIG_LPAPP
    { extern void lp_cli_register(void); lp_cli_register(); }   // `lpsleep`: one PDS15 lp_fw sleep cycle (LP_FW=ON)
#endif
#if defined(AT_PROJECT_AWS)
    // Shared `ota_http <url>` (bflb_flash + pt_table + inline-xz slot writer). Linked on the AWS chips
    // (bl602/bl616/bl616cl): it needs the mbedTLS/picotls TLS transport for https, which - like aws_iot -
    // is only linked with the AWS stack (AWS=ON). AT_PROJECT_AWS is defined for exactly those chips.
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
#if defined BL602 || defined BL702 || defined BL616 || defined BL616CL || defined BL618DG
    // The shared wifi_service `wifi` (forget/drop/status): every platform with a wifi_port_* HAL
    // (bl602/bl616/amebad) registers it.
    { extern void wifi_cli_register(void); wifi_cli_register(); }
#endif
#ifdef AT_PROJECT_AWS
    // AWS IoT provisioning + telemetry (`aws`): the AWS chips (bl602/bl616/bl616cl); same credential
    // model as AmebaD. AT_PROJECT_AWS is defined only for those chips + AWS=ON.
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

/* app_network_up_start_services(): shared Bouffalo+AmebaD implementation in the startup skeleton at
 * the bottom of this file (identical feature gating on both platforms). */

/* app_startup_run() (the shared skeleton, below) calls these in canonical order; the common middle
 * (env/time + console) is in the shared helper. These are the Bouffalo-specific steps. */

// Chip + logger + flash write-lock + WiFi firmware/event wiring + RF params + flash MTD. Runs first.
void app_platform_boot_init(void)
{
    board_init();
#if (defined(BL616) || defined(BL616CL) || defined(BL618DG)) && defined(CONFIG_LPAPP)
    {
        /* Debug aid (LP builds): mux the CKLink JTAG pins (TMS=IO0 TCK=IO1 TDO=IO14 TDI=IO15)
         * right after board_init so tickless/PDS issues can be captured live over JTAG (bl616
         * CKLink is debug-only; these pins are unused on this board). NB: PDS15 loses the GLB
         * pad mux — re-mux these raw (all four cfg = 0x40401a07) before any post-wake spin. */
        struct bflb_device_s *jtag_gpio = bflb_device_get_by_name("gpio");
        bflb_gpio_init(jtag_gpio, GPIO_PIN_0,  GPIO_FUNC_JTAG | GPIO_ALTERNATE | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_1);
        bflb_gpio_init(jtag_gpio, GPIO_PIN_1,  GPIO_FUNC_JTAG | GPIO_ALTERNATE | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_1);
        bflb_gpio_init(jtag_gpio, GPIO_PIN_14, GPIO_FUNC_JTAG | GPIO_ALTERNATE | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_1);
        bflb_gpio_init(jtag_gpio, GPIO_PIN_15, GPIO_FUNC_JTAG | GPIO_ALTERNATE | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_1);
    }
#endif
    log_lock_init();   // install the rxi-logger FreeRTOS-mutex lock before any log_* (SDK LOG_*/rfparam)
    // Lock bottom 3 MB of flash against accidental writes; must run after
    // flash driver init (board_init) and before anything that writes flash
    // (OTA / EasyFlash). Top 1 MB stays writable for PSM (LittleFS). The
    // cross-platform helper (component/flash_cli_config, over board_flash_protect
    // in bouffalo_sdk/board_flash_port) replaces the old at::lockFirmwareFlash;
    // board_flash_log_status() prints the resulting lock state at boot.
#if !defined(BL616) && !defined(BL616CL) && !defined(BL618DG)
    board_flash_lock_fw();
#endif
    // BL616/BL616CL: skip the boot-time WRITE lock -- keep the firmware region unlocked for now
    // (no re-lock -> no dependence on the eflash-loader clearing protection at reflash). The SR
    // read/write path itself IS now XIP-quiesced (board_flash_bouffalo.c), so an un-quiesced raw
    // WRSR/RDSR no longer races the XIP continuous-read here -- that race previously hung the boot
    // in Clock_Peripheral_Clock_Get and left a CMP-inverted, brick-for-reflash status register.
    board_flash_log_status();   // SR1/SR2 diag print (read path is XIP-quiesced on every chip)
#if defined BL606P || defined BL808 || defined BL616 || defined BL616CL || defined BL618DG
    hal_wifi_start_firmware_task();
#endif // defined BL606P || defined BL808 || defined BL616 || defined BL616CL || defined BL618DG
#if defined BL602 || defined BL702
    // Register the app WiFi-event handler with the wifi4 adapter's direct dispatcher (no async queue).
    // wifi_mgmr also registers its internal handler; platform_post_event fans out to both.
    platform_register_event(EV_WIFI, event_cb_wifi_event, NULL);
#endif // defined BL602 || defined BL702
#if defined BL616 || defined BL616CL || defined BL618DG
    // Register the app handler on the async EV_WIFI bus BEFORE fhost_init() (on_tcpip_init_done)
    // dispatches INIT_DONE/MGMR_DONE. (fhost + wifi_mgmr also register their own EV_WIFI filters.)
    async_register_event_filter(EV_WIFI, bl616_wifi_event_cb, NULL);
#endif // defined BL616 || defined BL616CL || defined BL618DG
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

#if defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616 || defined BL616CL || defined BL618DG
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
        /* fall through -> country auto-detect + wifi_connect */
    }
#endif

    /* If no regulatory domain was provisioned, scan the surrounding APs, take the majority beacon
     * Country IE, save it to WIFI_COUNTRY, and set the matching channel plan now (reboots, then
     * reconnects with it). Runs before associating so the reboot precedes the connection. */
    dot11d_autodetect_country();   /* component/80211d_config (port: component/amebad/80211d_port) */

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
    { extern void dsleep_cli_register(void); dsleep_cli_register(); }           /* shared `dsleep [uart] [rtc <s>]` (component/dsleep_config) */
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

/* app_network_up_start_services(): shared Bouffalo+AmebaD implementation in the startup skeleton at
 * the bottom of this file (identical feature gating on both platforms). */

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

#if defined(AT_MAIN_BOUFFALO) || defined(AT_MAIN_AMEBAD)
/* Network-up services, shared by Bouffalo (bl602/bl616/bl616cl) + AmebaD: the feature gating is
 * identical on both platforms (was two per-platform strong overrides of the weak default above). */
void app_network_up_start_services(void)
{
#ifdef AT_PROJECT_QUIC
    /* QUIC build: run the ngtcp2 + picotls/mbedTLS TLS1.3 handshake/perf self-test (the console and
     * iperf TUs are not linked in this image). */
    { extern void quic_test_start(void); quic_test_start(); }
#else
    /* coreSNTP UTC time-sync (default-on in every non-QUIC image, idempotent): an async task that
     * fetches UTC and backs newlib's _gettimeofday (time_base.c), so logs / the TLS server-cert
     * validity check / OTA see real wall-clock time. */
    { extern void sntp_sync_start(void); sntp_sync_start(); }
#if defined(AT_PROJECT_AWS_IOT)
    /* Full AWS IoT agent (AWSOTA=ON): coreMQTT-Agent + telemetry + OTA over mutual TLS. Idempotent
     * + a no-op until provisioned, so it Just Works after `aws endpoint/ca/cert/key ...`; the
     * connect loop self-heals until SNTP supplies real UTC. */
    { extern void aws_iot_demo_start(void); aws_iot_demo_start(); }
#elif defined(AT_PROJECT_AWS)
    /* Lean AWS image (AWS=ON alone): the telemetry-only client. */
    { extern void aws_telemetry_start(void); aws_telemetry_start(); }
#endif
    /* iperf: manual `iperf server|tx|udp` console command (registered when linked) -- no auto-start
     * at network-up on any platform. */
#endif /* AT_PROJECT_QUIC */
}
#endif /* AT_MAIN_BOUFFALO || AT_MAIN_AMEBAD */

#ifdef AT_PROJECT_QUIC
/* Console-less startup for the QUIC self-test image: same platform hooks as app_startup_run() minus the
 * LittleFS/env/console path (that image drops the console libs); default TZ only so timestamps read
 * local time. The QUIC handshake self-test starts from the network-up hook. */
void app_startup_run_headless(void)
{
    app_platform_boot_init();
    /* Load persisted env even in the console-less image: WiFi provisioning (WIFI_SSID/WIFI_PASSWORD)
     * comes from /KV via env_init(), and the QUIC self-test's wsvc reads getenv("WIFI_SSID"). Without
     * it the LFS env is never loaded -> "no WIFI_SSID provisioned - staying offline" and the chip
     * never reaches the network for the QUIC perf run. app_env_time_init() itself is compiled out of
     * the QUIC image (#if !AT_PROJECT_QUIC), so inline its env-load here (vfs_lfs_mount + env_init are
     * both linked -- LFS is used by the bootstrap self-test; env_init's TU links because we call it).
     * (AmebaD's QUIC used a compile-time SSID; bl616 uses env -> a baked /KV image, lfs-image-preconfig,
     * provisions the console-less QUIC build.) */
    {
        extern void *vfs_lfs_mount(void);
        extern void  env_init(void);
        void *lfs = vfs_lfs_mount();
        printf("[LFS] mount %s\n", lfs ? "OK" : "FAILED");
        if (lfs != NULL)
            env_init();
    }
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
// Declared in component/syscall_config/syscalls.h; declared here directly to
// keep main.cpp's include surface unchanged across platforms.
extern "C" void at_libc_early_init( void );

extern "C" int main(void)
{
    // Must be first: on a picolibc toolchain (bl618dg) this installs the thread
    // pointer + TLS block that libc's errno / _localtime_buf live in. No-op
    // (weak) on the newlib platforms. This startup has no .init_array, so it
    // cannot be a constructor — it has to be an explicit call.
    at_libc_early_init();

#ifdef AT_PROJECT_QUIC
    app_startup_run_headless();   // console-less self-test image (layout-sensitive)
#else
    app_startup_run();
#endif
    return 0;
}
#endif // !AT_SDK_OWNS_MAIN
