#include <stdio.h>
#include <string.h>
#include "board.h"
#include "FreeRTOS.h"
#include "task.h"
#include "btble_lib_api.h"
#include "rfparam_adapter.h"
#include "bflb_mtd.h"
#include "easyflash.h"
#include "bluedroid_bouffalo.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_pan_api.h"
#include "bluedroid_pan_tcp.h"
#include "shell.h"
#include "bflb_uart.h"
#ifdef CONFIG_PAN_COEX
#include "pan_coex.h"
#include "bouffalo_wifi_tcp.h"
#else
#include "bflb_gpio.h"
#endif

#if defined(CONFIG_PAN_GATT_BR) || defined(CONFIG_PAN_GATT_LE)
extern void gatt_br_load_init(void);
#endif

#ifdef CONFIG_PAN_SPP
extern void spp_load_init(void);
#endif

#if !defined(CONFIG_PAN_SPP) && !defined(CONFIG_PAN_GATT_BR) && !defined(CONFIG_PAN_GATT_LE)
static uint16_t connected_handle = ESP_PAN_INVALID_HANDLE;
static bool pan_ready;
#endif
extern void shell_init_with_task(struct bflb_device_s *uart);

#ifndef CONFIG_PAN_GATT_LE
static void gap_event(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    if (event == ESP_BT_GAP_AUTH_CMPL_EVT)
        printf("[pan] auth status=%u\r\n", param->auth_cmpl.stat);
    else if (event == ESP_BT_GAP_CFM_REQ_EVT)
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
    else if (event == ESP_BT_GAP_DISC_RES_EVT)
        printf("[pan] discovered %02x:%02x:%02x:%02x:%02x:%02x\r\n",
               param->disc_res.bda[0], param->disc_res.bda[1], param->disc_res.bda[2],
               param->disc_res.bda[3], param->disc_res.bda[4], param->disc_res.bda[5]);
    else
        printf("[pan] GAP event=%u\r\n", event);
}

#endif

#if !defined(CONFIG_PAN_SPP) && !defined(CONFIG_PAN_GATT_BR) && !defined(CONFIG_PAN_GATT_LE)
static void pan_event(esp_pan_cb_event_t event, esp_pan_cb_param_t *param)
{
    bluedroid_pan_tcp_event(event, param);
    if (event == ESP_PAN_DATA_IND_EVT || event == ESP_PAN_WRITE_EVT || event == ESP_PAN_CONG_EVT)
        return;
    if (event == ESP_PAN_INIT_EVT)
        printf("[pan] INIT status=%u\r\n", param->init.status);
    else if (event == ESP_PAN_SET_ROLE_EVT) {
        pan_ready = param->set_role.status == ESP_PAN_SUCCESS;
        printf("[pan] ROLE status=%u roles=%u\r\n", param->set_role.status, param->set_role.role);
    } else if (event == ESP_PAN_OPEN_EVT) {
        if (param->open.status == ESP_PAN_SUCCESS) connected_handle = param->open.handle;
        printf("[pan] OPEN status=%u handle=%u\r\n", param->open.status, param->open.handle);
    } else if (event == ESP_PAN_CLOSE_EVT) {
        connected_handle = ESP_PAN_INVALID_HANDLE;
        printf("[pan] CLOSED\r\n");
    }
    else
        printf("[pan] profile event=%u\r\n", event);
}

#endif

static void start_host(void *unused)
{
    (void)unused;
    bflb_mtd_init();
    configASSERT(easyflash_init() == EF_NO_ERR);
    #ifdef CONFIG_PAN_COEX
    pan_coex_prepare();
    pan_coex_start_wifi();
    #else
    configASSERT(rfparam_init(0, NULL, 0) == 0);
    bluedroid_bouffalo_use_combo_rf();
    #ifdef BL618DG
    struct bflb_device_s *gpio = bflb_device_get_by_name("gpio");
    bflb_gpio_set(gpio, GPIO_PIN_16);
    bflb_gpio_init(gpio, GPIO_PIN_16, GPIO_FUNC_GPIO | GPIO_OUTPUT | GPIO_PULLUP | GPIO_SMT_EN);
    bflb_gpio_set(gpio, GPIO_PIN_16);
    printf("[pan] BT-only: GPIO16 HIGH\r\n");
    #endif
    #endif
    btble_controller_init(configMAX_PRIORITIES - 1);
#ifndef CONFIG_PAN_GATT_LE
    if (!bluedroid_bouffalo_classic_available()) {
        printf("[bt] " BLUEDROID_BOARD_NAME " controller reports BR/EDR disabled; PAN/SPP unavailable\r\n");
        vTaskDelete(NULL);
        return;
    }
#endif
    configASSERT(bluedroid_bouffalo_hci_attach() == ESP_OK);
    printf("[pan] Bluedroid init\r\n");
    esp_err_t status = esp_bluedroid_init();
    printf("[pan] init=%d heap=%u\r\n", status, (unsigned)xPortGetFreeHeapSize());
    if (!status) status = esp_bluedroid_enable();
    printf("[pan] enable=%d heap=%u\r\n", status, (unsigned)xPortGetFreeHeapSize());
    if (!status) {
        const uint8_t *address = esp_bt_dev_get_address();
        printf("[bt] address=%02x:%02x:%02x:%02x:%02x:%02x\r\n",
               address[0], address[1], address[2], address[3], address[4], address[5]);
#if !defined(CONFIG_PAN_SPP) && !defined(CONFIG_PAN_GATT_BR) && !defined(CONFIG_PAN_GATT_LE)
        configASSERT(bluedroid_pan_tcp_register(esp_bt_dev_get_address()));
        configASSERT(FreeRTOS_IPInit_Multi() == pdPASS);
        #ifdef CONFIG_PAN_COEX
        bouffalo_wifi_tcp_started();
        #endif
#endif
#ifndef CONFIG_PAN_GATT_LE
        esp_bt_gap_register_callback(gap_event);
#if !defined(CONFIG_PAN_SPP) && !defined(CONFIG_PAN_GATT_BR) && !defined(CONFIG_PAN_GATT_LE)
        esp_pan_register_callback(pan_event);
#endif
        esp_bt_gap_set_device_name(BLUEDROID_BOARD_NAME "-PAN");
        esp_bt_io_cap_t capability = ESP_BT_IO_CAP_NONE;
        esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &capability, sizeof(capability));
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
#endif
#if defined(CONFIG_PAN_GATT_LE)
        gatt_br_load_init();
#elif defined(CONFIG_PAN_GATT_BR)
        esp_bt_gap_set_device_name(BLUEDROID_BOARD_NAME "-GATT-BR");
        gatt_br_load_init();
#elif defined(CONFIG_PAN_SPP)
        esp_bt_gap_set_device_name(BLUEDROID_BOARD_NAME "-SPP");
        spp_load_init();
#else
        esp_pan_cfg_t cfg = ESP_PAN_DEFAULT_CONFIG();
        status = esp_pan_init(&cfg);
        printf("[pan] PANU init request=%d\r\n", status);
#endif
    }
    vTaskDelete(NULL);
}

int main(void)
{
    board_init();
    shell_init();
    shell_init_with_task(bflb_device_get_by_name("uart0"));
#if defined(CONFIG_PAN_GATT_LE)
    printf("[gatt-le] " BLUEDROID_BOARD_NAME " GATT over BLE validation\r\n");
#elif defined(CONFIG_PAN_GATT_BR)
    printf("[gatt-br] " BLUEDROID_BOARD_NAME " GATT over BR/EDR validation\r\n");
#elif defined(CONFIG_PAN_SPP)
    printf("[spp] " BLUEDROID_BOARD_NAME " Bluedroid SPP-only\r\n");
#else
    printf("[pan] " BLUEDROID_BOARD_NAME " Bluedroid PANU\r\n");
#endif
    /* Match the vendor startup worker; FHOST/WPA must run during bring-up. */
    configASSERT(xTaskCreate(start_host, "pan_start", 2048, NULL,
                            10, NULL) == pdPASS);
    vTaskStartScheduler();
    for (;;) {}
}

#if !defined(CONFIG_PAN_SPP) && !defined(CONFIG_PAN_GATT_BR) && !defined(CONFIG_PAN_GATT_LE)
static int pan_connect(int argc, char **argv)
{
    if (!pan_ready || argc != 2) { printf("usage: pan_connect XX:XX:XX:XX:XX:XX\r\n"); return -1; }
    unsigned bytes[6];
    int consumed = 0;
    if (strlen(argv[1]) != 17 || sscanf(argv[1], "%2x:%2x:%2x:%2x:%2x:%2x%n",
        &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5], &consumed) != 6 || consumed != 17)
        return -1;
    esp_bd_addr_t address;
    for (unsigned i = 0; i < 6; ++i) address[i] = bytes[i];
    int result = esp_pan_connect(address, ESP_PAN_ROLE_PANU, ESP_PAN_ROLE_NAP);
    printf("[pan] connect request=%d\r\n", result);
    return result;
}
SHELL_CMD_EXPORT_ALIAS(pan_connect, pan_connect, Connect PANU to remote NAP.);

static int pan_status(int argc, char **argv)
{
    (void)argc; (void)argv;
    const uint8_t *address = esp_bt_dev_get_address();
    if (address) printf("[pan] address=%02x:%02x:%02x:%02x:%02x:%02x bonds=%d\r\n",
        address[0], address[1], address[2], address[3], address[4], address[5],
        esp_bt_gap_get_bond_device_num());
    bluedroid_pan_tcp_status();
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(pan_status, pan_status, Show PAN link and packet counters.);

static int pan_disconnect(int argc, char **argv)
{ (void)argc; (void)argv; return esp_pan_disconnect(connected_handle); }
SHELL_CMD_EXPORT_ALIAS(pan_disconnect, pan_disconnect, Disconnect PAN.);

static int pan_scan(int argc, char **argv)
{ (void)argc; (void)argv; return esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 8, 0); }
SHELL_CMD_EXPORT_ALIAS(pan_scan, pan_scan, Discover classic Bluetooth peers.);

#endif
