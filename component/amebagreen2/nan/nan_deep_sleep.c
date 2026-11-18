/* SoC DSLP loses the running host. Each AON/power wake boots NAN normally;
 * only the pairing record in LittleFS survives. No BLE controller is started. */
#include "ameba_soc.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

static uint32_t wake_ms = AT_NAN_DEEP_SLEEP_MS;
static int active;
static TickType_t started;
extern void amebagreen2_console_sleep(int enter);
extern int green2_nan_request_standby(void);

void green2_nan_wait_for_start(void)
{
    started = xTaskGetTickCount();
    uint32_t reason = BOOT_Reason();
    uint32_t aon = SOCPS_AONWakeReason();
    /* A timed DSLP wake restarts both cores. Clear the latched timer before
     * allowing the next sleep cycle; do not depend on a retained ISR. */
    RCC_PeriphClockCmd(APBPeriph_ATIM, APBPeriph_ATIM_CLOCK, ENABLE);
    AONTimer_INT(DISABLE);
    AONTimer_ClearINT();
    RCC_PeriphClockCmd(APBPeriph_ATIM, APBPeriph_ATIM_CLOCK, DISABLE);
    printf("[NAN-DEEP] boot reason=%08lx aon=%08lx source=%s next_sleep_ms=%lu\n",
           (unsigned long)reason, (unsigned long)aon,
           (reason & AON_BIT_RSTF_DSLP) ? "dslp" : "reset/power",
           (unsigned long)wake_ms);
}

void green2_nan_start_result(int rc)
{
    active = !rc;
    printf("[NAN-DEEP] NAN active rc=%d startup_ms=%lu uptime_ms=%lu\n", rc,
           (unsigned long)((xTaskGetTickCount() - started) * portTICK_PERIOD_MS),
           (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS));
}

int green2_nan_power_can_pause(void)
{
    /* Never silently override another subsystem's sleep veto. */
    return active && !(pmu_get_wakelock_status() & ~BIT(PMU_OS)) &&
           !(pmu_get_deepwakelock_status() & ~BIT(PMU_OS));
}

int green2_nan_deep_request(uint32_t seconds)
{
    uint32_t previous = wake_ms;
    if (seconds > 3600) return -1;
    wake_ms = seconds * 1000u;
    if (!green2_nan_request_standby()) return 0;
    wake_ms = previous;
    return -1;
}

void green2_nan_power_wait(void)
{
    active = 0;
    printf("[NAN-DEEP] entering DSLP wake_ms=%lu locks=%08lx deep_locks=%08lx\n",
           (unsigned long)wake_ms, (unsigned long)pmu_get_wakelock_status(),
           (unsigned long)pmu_get_deepwakelock_status());
    vTaskDelay(pdMS_TO_TICKS(100)); /* drain serial output before power removal */
    amebagreen2_console_sleep(1);
    pmu_set_max_sleep_time(wake_ms); /* zero maps to PMU_SLEEP_FOREVER */
    pmu_set_sleep_type(SLEEP_PG);
    pmu_set_dsleep_active_time(0);
    pmu_set_sysactive_time(0);
    pmu_release_deepwakelock(PMU_OS);
    pmu_release_wakelock(PMU_OS);
    /* Tickless sends dlps_enable to NP, which programs AON and enters DSLP.
     * This call cannot resume: wake goes through ROM/bootloader again. */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        printf("[NAN-DEEP] entry pending locks=%08lx deep_locks=%08lx\n",
               (unsigned long)pmu_get_wakelock_status(),
               (unsigned long)pmu_get_deepwakelock_status());
    }
}

void green2_nan_power_resume_result(int rc)
{
    /* Only reachable if radio teardown failed before DSLP was entered. */
    active = 0;
    printf("[NAN-DEEP] shutdown failed rc=%d; staying awake\n", rc);
}
