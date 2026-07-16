#ifndef TICKLESS_H
#define TICKLESS_H

#include <stdint.h>

int tickless_enter(void);
int tickless_exit(void);
int *tickless_handke_get(void);

/* Sleep-attempt counters (tickless.c): where each vApplicationSleep attempt ended. Written from
 * the idle task with interrupts masked (printf is unsafe there); `tickless stats` prints them. */
struct tickless_stats {
    uint32_t attempt;      /* vApplicationSleep entries with tickless enabled */
    uint32_t abort_confirm;/* eTaskConfirmSleepModeStatus == eAbortSleep */
    uint32_t abort_ready;  /* check_system_ready_for_LowPower() said no */
    uint32_t abort_ringbuf;/* console TX ringbuffer not drained (late recheck) */
    uint32_t abort_hook;   /* a registered hook vetoed (allow_enter / prepare_enter) */
    uint32_t enter_fail;   /* bl_lp_fw_enter() < 0 */
    uint32_t slept;        /* bl_lp_fw_enter() returned >= 0 (a real PDS15 cycle) */
    /* abort_ready sub-causes (check_system_ready_for_LowPower branches, in order) */
    uint32_t r_common;     /* macswl_ps_sleep_common_check (WiFi TX/RX busy) */
    uint32_t r_ringbuf;    /* console TX ringbuffer not empty (early check) */
    uint32_t r_32k;        /* 32k clock / trim not ready */
    uint32_t r_bcn;        /* beacon-delay estimate not ready */
    uint32_t r_event;      /* bl_pm_event_get() or a CONNECT task exists */
    uint32_t r_ps;         /* macswl_ps_sleep_check (WiFi not in PS sleep state) */
    uint32_t r_cfg;        /* bl_pm_wifi_config_get failed / bcn_dtim window check */
};
extern struct tickless_stats tickless_stats;

/* BLE gate counters (tickless_ble.c, NIMBLE=ON builds). Incremented from the idle task inside
 * vApplicationSleep (printf is unsafe there); `tickless stats` prints them. */
struct tickless_ble_stats {
    uint32_t query;        /* prepare_enter ran with the controller up */
    uint32_t veto;         /* BLE active (btble_controller_sleep(0) != 0) -> PDS vetoed */
    int32_t last_ble_rtc;  /* last btble_controller_sleep(0) result, RTC cycles */
};
extern struct tickless_ble_stats tickless_ble_stats;

#endif
