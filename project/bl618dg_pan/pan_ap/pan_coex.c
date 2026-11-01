#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "wifi_mgmr.h"
#include "wifi_mgmr_ext.h"
#include "wifi_mgmr_coex.h"
#include "macsw_plat.h"
#include "fhost_api.h"
#include "async_event.h"
#include "bouffalo_wifi_tcp.h"
#include "pan_coex.h"
#include "shell.h"
#include "bflb_gpio.h"
static volatile bool wifi_ready;
static void wifi_event(async_input_event_t event, void *unused)
{
    (void)unused;
    printf("[wifi] event=%u\r\n", event->code);
    if (event->code == CODE_WIFI_ON_INIT_DONE) wifi_mgmr_task_start();
    else if (event->code == CODE_WIFI_ON_MGMR_DONE) wifi_ready = true;
    else if (event->code == CODE_WIFI_ON_SCAN_DONE) wifi_mgmr_sta_scanlist();
}
#ifndef CONFIG_PAN_WIFI_ONLY
void pan_coex_prepare(void)
{
#ifdef CONFIG_PAN_COEX_COMBO
    /* Keep the tested BT antenna selection until the external switch is tested. */
    struct bflb_device_s *gpio = bflb_device_get_by_name("gpio");
    bflb_gpio_set(gpio, GPIO_PIN_16);
    bflb_gpio_init(gpio, GPIO_PIN_16, GPIO_FUNC_GPIO | GPIO_OUTPUT | GPIO_SMT_EN);
    bflb_gpio_set(gpio, GPIO_PIN_16);
    configASSERT(wifi_mgmr_coex_board_configure(WIFI_MGMR_COEX_BOARD_COMBO_SHARED_PATH, -1) == WIFI_MGMR_COEX_BOARD_CONFIG_OK);
    configASSERT(wifi_mgmr_coex_rf_prepare(WIFI_MGMR_COEX_RF_COMBO) == WIFI_MGMR_COEX_OK);
    printf("[coex] SDK combo RF; external GPIO16 fixed BT HIGH (validation)\r\n");
#else
    configASSERT(wifi_mgmr_coex_board_configure(WIFI_MGMR_COEX_BOARD_STANDALONE_SINGLE_ANT_SPDT, 16) == WIFI_MGMR_COEX_BOARD_CONFIG_OK);
    configASSERT(wifi_mgmr_coex_rf_prepare(WIFI_MGMR_COEX_RF_STANDALONE) == WIFI_MGMR_COEX_OK);
    printf("[coex] official SDK standalone single antenna, GPIO16 SPDT (BT high)\r\n");
#endif
}
#ifdef CONFIG_PAN_COEX_COMBO
static int combo_ant(int argc, char **argv)
{
    if (argc != 2) return -1;
    struct bflb_device_s *gpio = bflb_device_get_by_name("gpio");
    if (!strcmp(argv[1], "pta")) {
        bflb_gpio_init(gpio, GPIO_PIN_16, GPIO_FUNC_SPDT | GPIO_ALTERNATE);
    } else if (!strcmp(argv[1], "bt") || !strcmp(argv[1], "wifi")) {
        bool bt = !strcmp(argv[1], "bt");
        if (bt) bflb_gpio_set(gpio, GPIO_PIN_16);
        else bflb_gpio_reset(gpio, GPIO_PIN_16);
        bflb_gpio_init(gpio, GPIO_PIN_16, GPIO_FUNC_GPIO | GPIO_OUTPUT | GPIO_SMT_EN);
        if (bt) bflb_gpio_set(gpio, GPIO_PIN_16);
        else bflb_gpio_reset(gpio, GPIO_PIN_16);
    } else return -1;
    printf("[coex] GPIO16 antenna=%s (combo board validation)\r\n", argv[1]);
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(combo_ant, combo_ant, Combo switch validation: bt|wifi|pta.);
#endif
#endif
void pan_coex_start_wifi(void)
{
    configASSERT(async_register_event_filter(EV_WIFI, wifi_event, NULL) == 0);
    printf("[wifi] starting MAC/FHOST\r\n");
    wifi_task_create();
    configASSERT(fhost_init() == 0);
    printf("[wifi] FHOST initialized\r\n");
    TickType_t start = xTaskGetTickCount();
    while (!wifi_ready || bouffalo_wifi_tcp_interfaces_ready() != pdPASS) {
        configASSERT(xTaskGetTickCount() - start < pdMS_TO_TICKS(15000));
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    printf("[wifi] MGMR ready\r\n");
}
static int wifi_scan(int argc, char **argv)
{
    (void)argc; (void)argv;
    wifi_mgmr_scan_params_t config = {0};
    int result = wifi_ready ? wifi_mgmr_sta_scan(&config) : -1;
    printf("[wifi] scan request=%d\r\n", result);
    return result;
}
SHELL_CMD_EXPORT_ALIAS(wifi_scan, wifi_scan, Scan Wi-Fi.);
static int wifi_connect(int argc, char **argv)
{
    if (!wifi_ready || argc < 3 || argc > 4) return -1;
    unsigned long frequency = 0;
    if (argc == 4) {
        char *end;
        frequency = strtoul(argv[3], &end, 10);
        if (*end || frequency < 2400 || frequency > 5900) return -1;
    }
    int result = wifi_sta_connect(argv[1], argv[2], NULL, NULL, 0, frequency, 0, 1);
    printf("[wifi] connect request=%d frequency=%lu\r\n", result, frequency);
    return result;
}
SHELL_CMD_EXPORT_ALIAS(wifi_connect, wifi_connect, Connect Wi-Fi: SSID password [frequency MHz].);
static int wifi_disconnect(int argc, char **argv)
{ (void)argc; (void)argv; return wifi_ready ? wifi_sta_disconnect() : -1; }
SHELL_CMD_EXPORT_ALIAS(wifi_disconnect, wifi_disconnect, Disconnect Wi-Fi STA.);
#ifndef CONFIG_PAN_WIFI_ONLY
static int coex_status(int argc, char **argv)
{
    (void)argc; (void)argv;
    struct wifi_mgmr_coex_status status;
    int result = wifi_mgmr_coex_status_get(&status);
    if (result) printf("[coex] status error=%d\r\n", result);
    else printf("[coex] hardware=%u active=%u ps_pta=%u runtime=%u band=%u duty=%u ms\r\n",
        status.hardware_configured, status.active, status.ps_pta_running,
        status.effective_runtime, status.band, status.duty_active_ms);
    bouffalo_wifi_tcp_status();
    return result;
}
SHELL_CMD_EXPORT_ALIAS(coex_status, coex_status, Show official coexistence and Wi-Fi status.);
static int coex_ps(int argc, char **argv)
{
    if (argc < 2 || argc > 3) return -1;
    int result;
    if (!strcmp(argv[1], "0") && argc == 2) result = wifi_mgmr_coex_stop();
    else if (!strcmp(argv[1], "1")) {
        unsigned long duty = 0;
        if (argc == 3) {
            char *end;
            duty = strtoul(argv[2], &end, 10);
            if (*end || duty < 10 || duty > 90) return -1;
        }
        struct wifi_mgmr_coex_status status;
        result = wifi_mgmr_coex_status_get(&status);
        if (result) return result;
        /* The SDK requires ordinary STA PS off before taking PS ownership. */
        if (!status.ps_pta_running && (result = wifi_mgmr_sta_ps_exit()) != 0) return result;
        if (duty && (result = wifi_mgmr_coex_duty_set(duty)) != 0) return result;
        result = wifi_mgmr_coex_start();
    } else return -1;
    printf("[coex] PS-PTA request result=%d\r\n", result);
    return result;
}
SHELL_CMD_EXPORT_ALIAS(coex_ps, coex_ps, PS-PTA: 1 [10-90 ms] start or 0 stop; 2.4 GHz STA only.);

#endif
static int wifi_status(int argc, char **argv)
{
    (void)argc; (void)argv;
    bouffalo_wifi_tcp_status();
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(wifi_status, wifi_status, Show Wi-Fi interface and traffic counters.);
