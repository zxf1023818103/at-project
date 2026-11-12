#include <stdio.h>
#include "board.h"
#include "FreeRTOS.h"
#include "task.h"
#include "rfparam_adapter.h"
#include "bflb_gpio.h"
#include "bflb_mtd.h"
#include "easyflash.h"
#include "btble_lib_api.h"
#include "bluedroid_bouffalo.h"
#include "esp_bt_main.h"
extern void bluedroid_coc_bench_init(void);

static void start(void *unused)
{
    (void)unused;
    bflb_mtd_init();
    int rc = easyflash_init();
    if (rc != EF_NO_ERR) goto failed;
    rc = rfparam_init(0, NULL, 0);
    if (rc) goto failed;
    bluedroid_bouffalo_use_combo_rf();
    struct bflb_device_s *gpio = bflb_device_get_by_name("gpio");
    bflb_gpio_set(gpio, GPIO_PIN_16);
    bflb_gpio_init(gpio, GPIO_PIN_16, GPIO_FUNC_GPIO | GPIO_OUTPUT | GPIO_PULLUP | GPIO_SMT_EN);
    bflb_gpio_set(gpio, GPIO_PIN_16);
    printf("[CoC-BD] combo RF, GPIO16 HIGH, Wi-Fi off\r\n");
    btble_controller_init(configMAX_PRIORITIES - 1);
    rc = bluedroid_bouffalo_hci_attach();
    if (rc) goto failed;
    rc = esp_bluedroid_init();
    if (!rc) rc = esp_bluedroid_enable();
    if (rc) goto failed;
    printf("[CoC-BD] Bluedroid enabled heap=%u\r\n", (unsigned)xPortGetFreeHeapSize());
    bluedroid_coc_bench_init();
    vTaskDelete(NULL);
    return;
failed:
    printf("[CoC-BD] startup failed: %d\r\n", rc);
    vTaskDelete(NULL);
}
int main(void)
{
    board_init();
    BaseType_t rc = xTaskCreate(start, "coc_start", 2048, NULL, 10, NULL);
    configASSERT(rc == pdPASS);
    if (rc != pdPASS) return 1;
    vTaskStartScheduler();
    for (;;) {}
}
