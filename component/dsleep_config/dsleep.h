/*
 * dsleep.h - one cross-platform interface for the `dsleep [uart] [rtc <seconds>]` console command:
 * a full low-power sleep that powers the chip down (loses WiFi) and wakes on a UART-RX edge and/or
 * an RTC/timer timeout.
 *
 * The command (dsleep_cli.c) is shared; each platform implements the one primitive board_dsleep_enter()
 * over its own deep-sleep API:
 *   - bl616  : PDS15 (pm_pds_mode_enter); UART wake = console RX GPIO22 PDS-GPIO-INT (async falling
 *              edge); RTC = the PDS 32 kHz timer. Wake RESETS the chip (reboot + WiFi reconnect).
 *   - bl602  : PDS (PDS_Mode_Enter); UART wake = the AON wake pad GPIO7 (bl602's console RX GPIO3 is
 *              not wake-capable, so the wake/UART-RX signal must be wired to GPIO7); RTC = sleepTime.
 *              Wake RESETS the chip.
 *   - AmebaD : the SOCPS deep-sleep AON domain has NO UART wake (LP_UART is powered off; the AON
 *              wakepin is high-active vs UART idle-high), so UART wake is only reachable via the LP
 *              RETENTION tier (SleepCG). AmebaD therefore uses retention sleep (pmu/tickless
 *              mechanism): LOGUART RX wakes it and it RESUMES IN PLACE (no reboot). RTC = a bounded
 *              wait during which the idle enters SleepCG.
 *
 * So on bl616/bl602 a successful entry never returns (the chip reboots); on AmebaD it returns on wake.
 */
#ifndef DSLEEP_H
#define DSLEEP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- platform primitive (one implementation per platform) ----------------------------------- */

/* Enter deep/low-power sleep armed with the requested wake source(s). `rtc_seconds == 0` means no
 * RTC/timer wake. At least one of (uart_wake, rtc_seconds) must be set — the caller guarantees it.
 * Return: <0 on error (e.g. UART wake unsupported on this chip, or entry rejected). On bl616/bl602 a
 * successful entry does NOT return (the chip reboots on wake); on AmebaD it returns 0 on wake
 * (resume-in-place). */
int board_dsleep_enter(bool uart_wake, uint32_t rtc_seconds);

/* ---- shared console command (dsleep_cli.c) -------------------------------------------------- */

void dsleep_cli_register(void);   /* registers the `dsleep` FreeRTOS-CLI command */

#ifdef __cplusplus
}
#endif

#endif /* DSLEEP_H */
