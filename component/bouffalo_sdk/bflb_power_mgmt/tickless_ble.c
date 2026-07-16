/* tickless_ble.c — BLE gate for the tickless.c PDS15 path (NIMBLE=ON + LP_FW=ON).
 *
 * Policy: BLE activity VETOES deep sleep. While the NimBLE controller has anything scheduled
 * (advertising or a connection), the idle task stays out of PDS15 (plain WFI; WiFi power-save
 * still runs at the radio level); once BLE goes quiet the full WiFi-retention PDS15 tickless
 * resumes.
 *
 * Why not sleep AROUND the BLE schedule (the tickless_accurate.c model — cap the RTC deadline at
 * the controller's next LL event and btble_controller_sleep_restore() on wake)? Verified on HW to
 * be unshippable on this crystal-less board:
 *   - The lpfw's real wake time can slip past the programmed RTC deadline (beacon-loss window
 *     extensions; RC32K ~1200 ppm — no XTAL32K here). ONE oversleep permanently breaks the
 *     controller's LL timing base: btble_controller_sleep(0) returns negative forever.
 *   - The vendor fix (recovery_ble: BTDM soft-reset + rwip_restore_ble_reg) wedges the NimBLE
 *     host (HCI state lost mid-transaction; the hang propagates through shared locks — even the
 *     console dies).
 *   - The vendor's own reference (wl_ble_lp demo) ships with the BLE arbitration compiled OUT
 *     (CFG_BLE_ENABLE is never defined anywhere in the SDK) — it was never validated.
 * Full forensics: the project_bl616_ble_tickless memory.
 *
 * Hook contract (tickless_hook.h): prepare_enter runs right before bl_lp_fw_enter in the idle
 * task with interrupts masked — NO printf here (the console write path faults in this context);
 * events are counted and `tickless stats` (fhost/lp_cli.c) reads them from task context.
 */

#include <stdint.h>
#include <stdbool.h>

#include "bl_lp.h"

#include "btble_lib_api.h"
#include "tickless.h"       /* struct tickless_ble_stats */
#include "tickless_hook.h"

/* Controller task handle, defined (.bss) inside the prebuilt btblecontroller .a. Non-zero once
 * btble_controller_init() ran — the gate for whether BLE participates at all. */
extern uint32_t rw_main_task_hdl;

/* Event counters, read by `tickless stats` (lp_cli.c). Written only from the idle task inside
 * vApplicationSleep's interrupt-masked window, so plain increments are race-free. */
struct tickless_ble_stats tickless_ble_stats;

static bool tickless_ble_prepare_enter(void *arg)
{
    (void)arg;

    if (rw_main_task_hdl == 0) {
        return true; /* controller never started (no NimBLE bring-up) — not our business */
    }

    /* btble_controller_sleep(0): <0 = controller busy right now, >0 = RTC cycles until its next
     * scheduled LL event (ADV/connection active), 0 = nothing scheduled. Only 0 may deep-sleep. */
    int32_t ble_rtc = btble_controller_sleep(0);
    tickless_ble_stats.query++;
    tickless_ble_stats.last_ble_rtc = ble_rtc;
    if (ble_rtc != 0) {
        tickless_ble_stats.veto++;
        return false;
    }
    return true;
}

void tickless_ble_hooks_init(void)
{
    tickless_hooks_register(NULL, NULL,
                            tickless_ble_prepare_enter, NULL,
                            NULL, NULL);
}
