/*
 * board_dsleep_amebad.c - AmebaD (RTL8721D KM4) impl of board_dsleep_enter() for the shared `dsleep
 * [uart] [rtc <seconds>]` command (component/dsleep_config, dsleep.h).
 *
 * Unlike bl616/bl602 (PDS deep sleep, reboot on wake), AmebaD's SOCPS deep-sleep AON domain has NO
 * UART wake (the LP_UART peripheral is powered off, and the AON REGU wakepin is high-active vs a UART
 * RX line that idles high). The only UART wake is the LP RETENTION tier (SleepCG), which resumes in
 * place. So this drives the same pmu/tickless retention path the `tickless` command uses: releasing
 * the OS wakelock lets the FreeRTOS idle enter SleepCG, where the LOGUART RX IRQ (UART_LOG_IRQ_LP)
 * wakes it. We poll in short steps so the command RETURNS (resume-in-place) on a console keypress
 * (uart_wake) and/or after rtc_seconds; between polls the idle enters SleepCG for the low-power.
 */
#include "dsleep.h"

#include "FreeRTOS.h"
#include "task.h"
#include "ameba_soc.h"   /* pmu_* (via freertos_pmu.h) / SLEEP_CG / PMU_OS / LOGUART_Readable */

int board_dsleep_enter(bool uart_wake, uint32_t rtc_seconds)
{
    /* Enter the SleepCG retention tier when idle (LOGUART RX is the wake source, resume in place). */
    pmu_set_sleep_type(SLEEP_CG);
    pmu_release_wakelock(PMU_OS);

    const uint32_t step_ms = 100;                                  /* poll granularity / UART latency */
    const uint32_t limit_ms = rtc_seconds ? rtc_seconds * 1000u : 0;  /* 0 = no timeout (uart-only) */
    uint32_t waited_ms = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(step_ms));      /* the idle task enters SleepCG during this window */
        if (uart_wake && LOGUART_Readable()) {   /* a console keypress arrived */
            break;
        }
        waited_ms += step_ms;
        if (limit_ms && waited_ms >= limit_ms) { /* rtc timeout */
            break;
        }
    }

    pmu_acquire_wakelock(PMU_OS);   /* stop the tickless retention; stay awake */
    return 0;                       /* resumed in place */
}
