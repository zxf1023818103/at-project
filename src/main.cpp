#include "Common.h"
#include "AtTask.h"
#include "TestTask.h"
#include "BoardFlashLock.h"

extern "C" {

__attribute__((__weak__)) void *__dso_handle = (void *)&__dso_handle;

int wifi_do_fast_connect(void) {
    // at::make_shared<at::AtTask>()->startAsync("AtTask", 256 * 12, 4);
    at::make_shared<at::TestTask>()->startAsync("TestTask", 256 * 12, 4);
    return 0;
}

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
static void bl602_wifi_connect_task(void *arg);   // defined below main; used in wifi_event_handler
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
            // HW DHCP bring-up test: associate to the test AP from a task, NOT here.
            // wifi_event_handler runs with the scheduler suspended, and
            // wifi_mgmr_sta_connect() blocks on a semaphore (connect result) -> asserts
            // if called directly. The connect triggers wifi4 STA-enable ->
            // wifi_eth_sta_enable -> bl602_tcp_bringup() (FreeRTOS+TCP IPInit), then the
            // connect indication kicks DHCP. TODO: proper provisioning (env / AT command).
            xTaskCreate(bl602_wifi_connect_task, (char *)"wifi_conn", 1024, NULL,
                        tskIDLE_PRIORITY + 2, NULL);
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
            break;
        }
        case CODE_WIFI_ON_PRE_GOT_IP: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_PRE_GOT_IP\n", __func__);
            break;
        }
        case CODE_WIFI_ON_GOT_IP: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_GOT_IP\n", __func__);
            break;
        }
        case CODE_WIFI_ON_DISCONNECT: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_DISCONNECT\n", __func__);
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
// HW DHCP test: associate to the test AP from a normal task context (blocking connect
// must not run in the scheduler-suspended wifi_event_handler).
static void bl602_wifi_connect_task(void *arg) {
    (void)arg;
    // CRITICAL: wifi_mgmr_sta_enable() must run BEFORE connecting. It calls
    // wifi_eth_sta_enable(), which reads the efuse MAC into wifiMgmr.wlan_sta.mac.
    // Without it wlan_sta.mac stays all-zero, so the connect handler's
    // bl_main_if_add(1, wlan_sta.mac) programs the STA vif with a ZERO MAC -> the
    // firmware builds the 802.11 auth request with source address 00:00:00:00:00:00
    // -> the AP never answers -> "auth or associate frame response timeout".
    // The vendor's wifi_sta_connect CLI calls wifi_mgmr_sta_enable() first; our
    // connect code was skipping it. (Confirmed on HW via gdb: the transmitted auth
    // frame's addr2/SA was 00:00:00:00:00:00.)
    (void)wifi_mgmr_sta_enable();   // side effect: loads efuse MAC into wlan_sta.mac
    // Scan first: the wifi4 connect uses cached scan results; without a scan it
    // fails "SSID error, scan no bssid and channel". Give the scan time to complete.
    wifi_mgmr_scan(NULL, NULL);
    vTaskDelay(pdMS_TO_TICKS(4000));
    wifi_mgmr_sta_connect(NULL, (char *)"ZengOTA", (char *)"123456789",
                          NULL, NULL, 0, 0);
    vTaskDelete(NULL);
}
#endif

int main(void) {
    board_init();
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
    // Mount LittleFS (PSM) via the shared VFS hook + load POSIX env from /KV --
    // same libc/env model as AmebaD (replaces easyflash_init; vfs_lfs_mount is
    // idempotent, so the lazy mount from the first open() is a no-op after this).
    if (vfs_lfs_mount() != nullptr) {
        env_init();
    }
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

    at::make_shared<at::AtTask>()->startAsync("AtTask", 256 * 12, 4);

#if defined(CONFIG_NIMBLE_HOST)
    xTaskCreate(bflb_nimble_boot_task, (char *)"nimble_boot", 1024, NULL, 3, NULL);
#endif // CONFIG_NIMBLE_HOST

    vTaskStartScheduler();
    while (1) {}
    return 0;
}
#endif // defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616 || defined BL616CL

#if defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2 || defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C
void app_pre_example(void) {
    // Lock the firmware flash region (bottom 3 MB) against accidental writes
    // from glitches / brownouts. Must happen before WLAN / BT / OTA start.
    at::lockFirmwareFlash();
    at::unlockFirmwareFlash();

    // Call back from wlan driver when rssi is lower than threshold
    // p_roaming_start_callback = wifi_roaming_start;

#if defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C
    // WiFi fast-reconnect is disabled on Ameba D: its FAST_RECONNECT_DATA sector
    // (flash 0x3FF000) was folded into LittleFS when LFS grew to 1024 KB (the
    // FTL/settings cluster was removed after the BLE stack moved to NimBLE).
    // Leaving these callbacks unset stops wifi_do_fast_connect / the reconnect
    // writer from reading/writing that address (now LFS data); the SDK side is
    // also compiled out via CONFIG_EXAMPLE_WLAN_FAST_CONNECT=0 (platform_opts.h).
    // p_wlan_init_done_callback = wifi_do_fast_connect;
    // p_write_reconnect_ptr = wlan_write_reconnect_data_to_flash;
#endif // defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C
}
#endif // defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2 || defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C

} // extern "C"

#if defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616 || defined BL616CL
namespace std {

void __throw_bad_function_call() { my_assert("__throw_bad_function_call" == nullptr); }

} // namespace std
#endif // defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616 || defined BL616CL
