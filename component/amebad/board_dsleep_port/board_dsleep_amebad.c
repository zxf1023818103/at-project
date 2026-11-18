/*
 * board_dsleep_amebad.c - AmebaD (RTL8721D KM4) impl of board_dsleep_enter() for the shared `dsleep
 * [uart] [rtc <seconds>]` command (component/dsleep_config, dsleep.h).
 *
 * Unlike bl616/bl602 (PDS deep sleep, reboot on wake), AmebaD's SOCPS deep-sleep AON domain has NO
 * UART wake (the LP_UART peripheral is powered off, and the AON REGU wakepin is high-active vs a UART
 * RX line that idles high). The only UART wake is the LP RETENTION tier (SleepCG), which resumes in
 * place. So this drives the same pmu/tickless retention path the `tickless` command uses: releasing
 * the OS wakelock lets the FreeRTOS idle enter SleepCG, where the LOGUART RX IRQ (UART_LOG_IRQ_LP)
 * wakes it.
 *
 * UART wake detection: we do NOT poll LOGUART_Readable() - the console's LOGUART RX ISR
 * (monitor_port) drains the HW FIFO into a StreamBuffer the instant a byte lands, so a poll races the
 * ISR and never sees the byte (and that ISR must stay enabled - it is the SleepCG wake source). We
 * instead block on monitor_rx_get(), the console's own RX primitive, which reads from that
 * ISR-fed stream: a console keypress fires the RX IRQ -> wakes SleepCG -> ISR queues the byte ->
 * monitor_rx_get() returns. rtc is a bounded wait; both together wake on whichever fires first.
 */
#include "dsleep.h"

#include "FreeRTOS.h"
#include "task.h"
#include "ameba_soc.h"   /* pmu_* (via freertos_pmu.h) / SLEEP_CG / PMU_OS */
#include "monitor.h"     /* monitor_rx_get: blocks on the console RX stream (ISR-fed, SleepCG wake) */

int board_dsleep_enter(bool uart_wake, uint32_t rtc_seconds)
{
    /* Enter the SleepCG retention tier when idle (LOGUART RX is the wake source, resume in place). */
    pmu_set_sleep_type(SLEEP_CG);
    pmu_release_wakelock(PMU_OS);

    const uint32_t step_ms = 100;                                     /* monitor_rx_get block / poll */
    const uint32_t limit_ms = rtc_seconds ? rtc_seconds * 1000u : 0;  /* 0 = no timeout (uart-only) */
    uint32_t waited_ms = 0;

    if (uart_wake) {
        /* Drain the command's own line-terminator (the CLI engine leaves the trailing '\n' in the RX
         * stream) plus any buffered bytes, so the wait below blocks on the NEXT keypress, not on the
         * newline that submitted `dsleep uart`. Buffered bytes return fast; the first empty read costs
         * one ~step_ms block, then the drain is done. */
        uint8_t c;
        while (monitor_rx_get(&c) == 1) { /* discard */ }
    }

    for (;;) {
        if (uart_wake) {
            uint8_t c;
            if (monitor_rx_get(&c) == 1) {   /* a console keypress arrived -> resume */
                break;
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(step_ms));  /* the idle task enters SleepCG during this window */
        }
        waited_ms += step_ms;
        if (limit_ms && waited_ms >= limit_ms) { /* rtc timeout */
            break;
        }
    }

    pmu_acquire_wakelock(PMU_OS);   /* stop the tickless retention; stay awake */
    return 0;                       /* resumed in place */
}
