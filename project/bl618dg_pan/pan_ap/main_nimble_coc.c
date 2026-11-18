#include <stdio.h>
#include "board.h"
#include "FreeRTOS.h"
#include "task.h"
#include "rfparam_adapter.h"
#include "bflb_gpio.h"
extern void nimble_start(void);
extern void cmd_set_btble_combo(int argc, char **argv);
static void start(void *unused)
{
    (void)unused;
    int rc = rfparam_init(0, NULL, 0);
    if (rc) {
        printf("[NimBLE bench] RF init failed: %d\r\n", rc);
        vTaskDelete(NULL);
        return;
    }
    cmd_set_btble_combo(0,NULL);
    struct bflb_device_s *gpio=bflb_device_get_by_name("gpio");
    bflb_gpio_set(gpio,GPIO_PIN_16);
    bflb_gpio_init(gpio,GPIO_PIN_16,GPIO_FUNC_GPIO|GPIO_OUTPUT|GPIO_PULLUP|GPIO_SMT_EN);
    bflb_gpio_set(gpio,GPIO_PIN_16);
    printf("[NimBLE bench] existing NimBLE port; combo RF, GPIO16 HIGH; Wi-Fi off\r\n");
    nimble_start();
    vTaskDelete(NULL);
}
int main(void)
{
    board_init();
    BaseType_t rc = xTaskCreate(start, "coc_start", 2048, NULL, 10, NULL);
    configASSERT(rc == pdPASS);
    if (rc != pdPASS) return 1;
    vTaskStartScheduler();
    for(;;) {}
}
