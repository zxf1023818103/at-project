/* Retention SleepCG: the KM4NS PMC keeps WLAN running and gates idle KM4TZ.
 * UART and PMC-timer wake resume the existing FreeRTOS tasks in place. */
#include "ameba_soc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "monitor.h"
#include "dsleep.h"
#include <stdio.h>
int board_dsleep_enter(bool uart_wake,uint32_t seconds)
{
    if (seconds > 86400 || (!uart_wake && !seconds)) return -1;
    extern volatile uint32_t green2_sleep_count,green2_sleep_ms;
    uint32_t count=green2_sleep_count, ms=green2_sleep_ms;
    uint8_t c;
    if (uart_wake) while (monitor_rx_get(&c) == 1) {}
    extern void amebagreen2_console_sleep(int);
    amebagreen2_console_sleep(1);
    TickType_t start=xTaskGetTickCount(), timeout=pdMS_TO_TICKS(seconds*1000);
    pmu_set_sleep_type(SLEEP_CG);
    pmu_release_wakelock(PMU_OS);
    for (;;) {
        if (uart_wake) { if (monitor_rx_get(&c) == 1) break; }
        else vTaskDelay(pdMS_TO_TICKS(100));
        if (seconds && xTaskGetTickCount()-start >= timeout) break;
    }
    pmu_acquire_wakelock(PMU_OS);
    amebagreen2_console_sleep(0);
    printf("[DS] SleepCG entries=%lu elapsed=%lu ms\n",
        (unsigned long)(green2_sleep_count-count),(unsigned long)(green2_sleep_ms-ms));
    return green2_sleep_count != count ? 0 : -1;
}
