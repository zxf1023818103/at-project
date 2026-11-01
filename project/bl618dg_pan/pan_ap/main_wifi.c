#include <stdio.h>
#include "board.h"
#include "FreeRTOS.h"
#include "task.h"
#include "bflb_gpio.h"
#include "bflb_mtd.h"
#include "rfparam_adapter.h"
#include "bouffalo_wifi_tcp.h"
#include "pan_coex.h"
#include "shell.h"
extern void shell_init_with_task(struct bflb_device_s *uart);
extern void cmd_set_btble_combo(int argc, char **argv);

static void start_wifi(void *unused)
{
    (void)unused;
    bflb_mtd_init();
    struct bflb_device_s *gpio = bflb_device_get_by_name("gpio");
    bflb_gpio_reset(gpio, GPIO_PIN_16);
    bflb_gpio_init(gpio, GPIO_PIN_16, GPIO_FUNC_GPIO | GPIO_OUTPUT | GPIO_PULLDOWN | GPIO_SMT_EN);
    bflb_gpio_reset(gpio, GPIO_PIN_16);
    printf("[wifi] Wi-Fi only; GPIO16 LOW; Bluetooth disabled\r\n");
    cmd_set_btble_combo(0, NULL);
    configASSERT(rfparam_init(0, NULL, 0) == 0);
    printf("[wifi] RF calibration complete\r\n");
    pan_coex_start_wifi();
    configASSERT(FreeRTOS_IPInit_Multi() == pdPASS);
    bouffalo_wifi_tcp_started();
    printf("[wifi] FreeRTOS+TCP ready; use wifi_scan / wifi_connect\r\n");
    vTaskDelete(NULL);
}
int main(void)
{
    board_init();
    shell_init();
    shell_init_with_task(bflb_device_get_by_name("uart0"));
    configASSERT(xTaskCreate(start_wifi, "wifi_start", 2048, NULL, 10, NULL) == pdPASS);
    vTaskStartScheduler();
    for (;;) {}
}
