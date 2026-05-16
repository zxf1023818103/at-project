#include "Common.h"
#include "AppTask.h"

extern "C" {

__attribute__((__weak__)) void *__dso_handle = (void *)&__dso_handle;

int wifi_do_fast_connect(void) {
    at::make_shared<at::AppTask>()->startAsync("AppTask", 256 * 12, 4);
    return 0;
}

#if defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C
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
            wifi_do_fast_connect();
            break;
        }
        case CODE_WIFI_ON_SCAN_DONE: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_SCAN_DONE\n", __func__);
            break;
        }
        case CODE_WIFI_ON_CONNECTED: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_CONNECTED\n", __func__);
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
    LWIP_UNUSED_ARG(arg);
#if defined BL602 || defined BL702
    // wifi_task_create() starts the wifi_main thread (which services the IPC
    // command queue used by wifi_mgmr) AND posts CODE_WIFI_ON_INIT_DONE
    // internally. Without this, bl_send_reset() etc. enqueue commands that
    // nothing processes and block forever on the event-group ACK.
    wifi_task_create();
#endif // defined BL602 || defined BL702
}

int main(void) {
    board_init();
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
    easyflash_init();
    tcpip_init(on_tcpip_init_done, NULL);

    vTaskStartScheduler();
    while (1) {}
    return 0;
}
#endif // defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616 || defined BL616CL

#if defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C
void app_pre_example(void) {
    // Call back from wlan driver when rssi is lower than threshold
    // p_roaming_start_callback = wifi_roaming_start;

    // Call back from wlan driver after wlan init done
    p_wlan_init_done_callback = wifi_do_fast_connect;

    // Call back from application layer after wifi_connection success
    p_write_reconnect_ptr = wlan_write_reconnect_data_to_flash;
}
#endif // defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C

} // extern "C"

#if defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616 || defined BL616CL
namespace std {

void __throw_bad_function_call() { my_assert("__throw_bad_function_call" == nullptr); }

} // namespace std
#endif // defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616 || defined BL616CL
