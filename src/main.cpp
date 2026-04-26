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
static void event_cb_wifi_event(input_event_t *event, void *private_data) {
    (void) private_data;
    void wifi_event_handler(uint32_t code, uint32_t code2);
    wifi_event_handler(event->code, 0);
}

static void board_init(void) {
    bl_sys_init();
}
#endif // defined BL602 || defined BL702

#if defined BL606P || defined BL808 || defined BL616
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
#endif // defined BL606P || defined BL808 || defined BL616

#if defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616
void wifi_event_handler(uint32_t code, uint32_t code2) {
    (void) code2;
    switch (code) {
        case CODE_WIFI_ON_INIT_DONE: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_INIT_DONE\n", __func__);
            wifi_conf_t conf = { .country_code = "CN" };
#if defined BL602 || defined BL702
            wifi_mgmr_start_background(&conf);
#endif // defined BL602 || defined BL702
#if defined BL606P || defined BL808 || defined BL616
            wifi_mgmr_init(&conf);
#endif // defined BL606P || defined BL808 || defined BL616
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

static void on_tcpip_init_done(void *arg) {
    LWIP_UNUSED_ARG(arg);
#if defined BL602 || defined BL702
    aos_post_event(EV_WIFI, CODE_WIFI_ON_INIT_DONE, 0);
#endif // defined BL602 || defined BL702
}

int main(void) {
    board_init();
    hal_wifi_start_firmware_task();
#if defined BL602 || defined BL702
    aos_register_event_filter(EV_WIFI, event_cb_wifi_event, NULL);
#endif // defined BL602 || defined BL702
#if defined BL606P || defined BL808 || defined BL616
    if (0 != rfparam_init(0, NULL, 0)) {
        printf("rfparam_init() failed\n");
        return 0;
    }
    bflb_mtd_init();
#endif // defined BL606P || defined BL808 || defined BL616
    easyflash_init();
    tcpip_init(on_tcpip_init_done, NULL);

#if defined BL606P || defined BL808 || defined BL616
    vTaskStartScheduler();
    while (1) {}
#endif // defined BL606P || defined BL808 || defined BL616
    return 0;
}
#endif // defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616

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

#if defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616
namespace std {

void __throw_bad_function_call() { my_assert("__throw_bad_function_call" == nullptr); }

} // namespace std
#endif // defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616
