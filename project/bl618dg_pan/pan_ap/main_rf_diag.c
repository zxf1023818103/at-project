/* Isolated diagnostics: no Bluetooth host, association or IP traffic.
 * One calibration attempt per reset; no calibration completion is fabricated. */
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "bflb_gpio.h"
#include "bflb_mtd.h"
#include "bflb_efuse.h"
#include "rfparam_adapter.h"
#include "wl_api.h"
#include "macsw_plat.h"
#include "shell.h"
#include "btble_lib_api.h"
extern void shell_init_with_task(struct bflb_device_s *uart);
extern void cmd_set_btble_combo(int argc, char **argv);
extern void cmd_set_btble_standalone(int argc, char **argv);
static volatile uint32_t sampled_pc;
static volatile unsigned stage;
static bool attempted;
void vApplicationTickHook(void)
{ __asm__ volatile("csrr %0, mepc" : "=r"(sampled_pc)); }
static void snapshot(TimerHandle_t timer)
{
    (void)timer;
    if (stage == 0 || stage == 4) return;
    printf("[rfdiag] stage=%u pc=%08lx MAC=%08lx FCAL_CTRL=%08lx FCAL=%08lx\r\n",
           stage, (unsigned long)sampled_pc,
           (unsigned long)*(volatile uint32_t *)0x24b00120,
           (unsigned long)*(volatile uint32_t *)0x200040b4,
           (unsigned long)*(volatile uint32_t *)0x200040b8);
}
static void select_bt(bool bt)
{
    struct bflb_device_s *gpio = bflb_device_get_by_name("gpio");
    if (bt) bflb_gpio_set(gpio, GPIO_PIN_16);
    else bflb_gpio_reset(gpio, GPIO_PIN_16);
    bflb_gpio_init(gpio, GPIO_PIN_16, GPIO_FUNC_GPIO | GPIO_OUTPUT | GPIO_SMT_EN);
    if (bt) bflb_gpio_set(gpio, GPIO_PIN_16);
    else bflb_gpio_reset(gpio, GPIO_PIN_16);
}
static void calibrate(void *argument)
{
    unsigned mode = (uintptr_t)argument;
    bflb_mtd_init();
    wl_cfg_get((uint8_t *)WL_API_RMEM_ADDR);
    if (mode != 0) {
        select_bt(false);
        cmd_set_btble_combo(0, NULL);
        stage = 1;
        printf("[rfdiag] combo calibration begin\r\n");
        int result = rfparam_init(0, NULL, 0);
        printf("[rfdiag] combo calibration result=%d\r\n", result);
        configASSERT(result == 0);
    }
    if (mode == 2 || mode == 5) {
        printf("[rfdiag] initialize idle Wi-Fi MAC before standalone calibration\r\n");
        wifi_task_create();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    if (mode == 4 || mode == 5) {
        printf("[rfdiag] initialize idle Bluetooth controller before standalone calibration\r\n");
        btble_controller_init(configMAX_PRIORITIES - 1);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    select_bt(true);
    stage = 2;
    printf("[rfdiag] select standalone; mode=%u\r\n", mode);
    cmd_set_btble_standalone(0, NULL);
    if (mode == 3) {
        printf("[rfdiag] explicitly enable standalone RF domain via vendor API\r\n");
        wl_standalone_rf_set_status(1);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    stage = 3;
    printf("[rfdiag] standalone full calibration begin\r\n");
    int result;
    if (mode == 6) {
        /* Keep full calibration enabled; isolate BZ from WLAN reinitialization. */
        struct wl_cfg_t *cfg = wl_cfg_get((uint8_t *)WL_API_RMEM_ADDR);
        cfg->mode = WL_API_MODE_BZ;
        cfg->en_param_load = 1;
        cfg->en_full_cal = 1;
        result = wl_init();
    } else {
        result = rfparam_init(0, NULL, 0);
    }
    stage = 4;
    printf("[rfdiag] standalone full calibration result=%d\r\n", result);
    vTaskDelete(NULL);
}
static int rf_test(int argc, char **argv)
{
    const char *modes[] = {"direct", "combo", "mac", "power", "controller", "both", "bz"};
    if (argc != 2 || attempted) { printf("usage: rf_test direct|combo|mac|power|controller|both|bz; reset between attempts\r\n"); return -1; }
    for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        if (strcmp(argv[1], modes[i])) continue;
        attempted = true;
        return xTaskCreate(calibrate, "rf_cal", 4096, (void *)(uintptr_t)i, 10, NULL) == pdPASS ? 0 : -1;
    }
    return -1;
}
SHELL_CMD_EXPORT_ALIAS(rf_test, rf_test, Isolated RF startup order test; one attempt per reset.);
int main(void)
{
    board_init();
    shell_init();
    shell_init_with_task(bflb_device_get_by_name("uart0"));
    printf("[rfdiag] RF order isolation; no host or Wi-Fi connection\r\n");
    printf("[rfdiag] PHY %s\r\n", wl_get_version());
    bflb_efuse_device_info_type info = {0};
    bflb_efuse_get_device_info(&info);
    printf("[rfdiag] efuse package=%u version=%u flash=%u psram=%u; RF_LDO=%08lx\r\n",
           info.package, info.version, info.flash_info, info.psram_info,
           (unsigned long)*(volatile uint32_t *)0x2008f880);
    TimerHandle_t timer = xTimerCreate("rf_sample", pdMS_TO_TICKS(2000), pdTRUE, NULL, snapshot);
    configASSERT(timer && xTimerStart(timer, 0) == pdPASS);
    vTaskStartScheduler();
    for (;;) {}
}
