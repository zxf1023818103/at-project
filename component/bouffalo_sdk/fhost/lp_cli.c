/*
 * lp_cli.c - PDS15 deep-sleep console commands (LP_FW=ON / CONFIG_LPAPP only), registered from
 * main.cpp's app_console_register_platform().
 *
 * Two commands, two wake models:
 *
 *   pdssleep  - BARE PDS15 (no lp_fw), mirroring the SDK's pmu/bl616/pds_rtc example. Enters PDS15,
 *               the PDS internal 32 kHz timer wakes after 5 s, and the wake RESETS the chip -> clean
 *               reboot -> WiFi reconnect. This is the proven, working sleep+wake cycle on this board
 *               (verified on HW: sleep 5 s -> POR-style reboot -> "macsw mode: lp" -> network UP).
 *
 *   lpsleep   - lp_fw PDS15 (bl_lp_fw_enter), the WiFi-retention low-power path. Cleanly ENTERS PDS15
 *               (verified: console powers down). It does NOT self-wake here: bl_lp_fw_enter() resumes
 *               via its saved-context restore vector (iot2lp_para->app_entry = lp_fw_restore_cpu_para)
 *               instead of a reset, and that resume path (restore CPU regs + re-init flash/XIP/clocks
 *               without a POR) does not complete on this setup. The board is recovered by a reset.
 *
 * Getting even this far required two fixes elsewhere:
 *   1. A -O3 miscompile of lp_fw_save/restore_cpu_para() (bflb_sys/bl616/bl616_lp.c, now
 *      noinline+ATTR_TCM_SECTION) that hard-faulted bl_lp_fw_enter() at the CPU-context save.
 *   2. The 32 kHz clock the HBN RTC/PDS timer runs on: HBN_Enable_RTC_Counter() + app_clock_init()
 *      RC32K bring-up at startup (main.cpp / power_mgmt/clock_manager.c). Without it the RTC never
 *      counts (the board's external xtal32k fails to start, so it falls back to the internal RC32K).
 *   3. The lp_fw RTC-only branch passed a 0 PDS-timer count (bl616_lp.c) so it could never wake
 *      standalone; now it passes the RTC count like the tim_wakeup paths and the bare example.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"

#include "bl_lp.h"           /* bl_lp_sys_callback_register / bl_lp_fw_enter / lpfw_cfg / bl_lp_fw_bcn_loss_cfg_dtim_default */
#include "board.h"           /* board_recovery() */
#include "bl616_hbn.h"       /* HBN_Pin_WakeUp_Mask */
#include "bl616_aon.h"       /* AON_Output_Float_LDO15_RF */
#include "bl616_pm.h"        /* pm_pds_mask_all_wakeup_src / pm_pds_mode_enter / PM_PDS_LEVEL_15 */
#include "tickless.h"        /* tickless_enter / tickless_exit (bflb_power_mgmt) */

/* fhost WiFi power-save enter/exit (void variants); extern-declared to avoid pulling the heavy
 * wifi_mgmr_ext.h -> mac_types.h -> phy.h include chain into this small command TU. */
extern int wifi_mgmr_sta_ps_enter(void);
extern int wifi_mgmr_sta_ps_exit(void);

static int lp_enter_cb(void *arg)
{
    (void)arg;
    return 0;
}

static int lp_exit_cb(void *arg)
{
    (void)arg;
    /* PDS15 powered the main core down: recover system/peripheral clocks + the console on wake. */
    board_recovery();
    printf("[LP] woke from PDS15\r\n");
    return 0;
}

static BaseType_t cmd_lpsleep(char *out, size_t outlen, const char *cmd)
{
    (void)cmd;

    /* RTC-wake PDS15 via lp_fw, no WiFi retention (tim_wakeup_en=0), 5 s. iot2lp_para was set up once
     * at boot by bl_lp_init(); do NOT re-run bl_lp_fw_init() here — it re-zeroes the RC32K trim state
     * app_clock_init() calibrated at startup, which stalls the entry. bl_lp_fw_enter() does its own
     * dcache-clean + CPU-context save internally. (Enters PDS15; lp_fw resume-wake is incomplete — see
     * the file header. Use `pdssleep` for a proven sleep+wake.) */
    lpfw_cfg.tim_wakeup_en = 0;
    lpfw_cfg.rtc_timeout_us = 5ULL * 1000 * 1000;   /* 5 s */
    lpfw_cfg.rtc_wakeup_cmp_cnt = 0;

    bl_lp_sys_callback_register(lp_enter_cb, NULL, lp_exit_cb, NULL);

    printf("[LP] entering PDS15 via lp_fw (rtc 5 s) ...\r\n"); fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(50));   /* let the UART FIFO drain before the core powers down */

    int ret = bl_lp_fw_enter(&lpfw_cfg);

    printf("[LP] bl_lp_fw_enter returned ret=%d\r\n", ret); fflush(stdout);
    snprintf(out, outlen, "bl_lp_fw_enter ret=%d\r\n", ret);
    return pdFALSE;
}

static BaseType_t cmd_pdssleep(char *out, size_t outlen, const char *cmd)
{
    (void)cmd; (void)out; (void)outlen;

    printf("[LP] pdssleep: bare PDS15, wake resets the chip in ~5 s ...\r\n"); fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(50));   /* let the UART FIFO drain before the core powers down */

    pm_pds_mask_all_wakeup_src();
    AON_Output_Float_LDO15_RF();
    HBN_Pin_WakeUp_Mask(0xF);
    pm_pds_mode_enter(PM_PDS_LEVEL_15, 32768 * 5);   /* PDS 32k timer wakes (= resets) after 5 s */

    /* not reached — PDS15 wake resets the chip */
    return pdFALSE;
}

/* `tickless <dtim>` / `tickless off` — arm (or disarm) the FreeRTOS tickless-idle PDS15 path, so the
 * idle task auto-enters PDS15 and the lp_fw wakes on DTIM beacons WITHOUT dropping the WiFi association
 * (the fast-wake, WiFi-retention low-power path, mirroring the SDK wl_lp `tickless` command). This is
 * the ONLY thing that flips enable_tickless=1; without it configUSE_TICKLESS_IDLE=1 is inert (idle just
 * __WFI()s). dtim default = 1: this BL618 board has NO external 32 kHz crystal, so it runs on the
 * internal RC32K (higher jitter, ~1200 ppm), and a long DTIM lets the wake-time drift past the lp_fw's
 * ~2 ms compensation cap -> missed beacons / packet loss. Keep DTIM small (raise cautiously while
 * watching for drops). Must be run AFTER WiFi is associated + got IP. */
static BaseType_t cmd_tickless(char *out, size_t outlen, const char *cmd)
{
    BaseType_t len = 0;
    const char *arg = FreeRTOS_CLIGetParameter(cmd, 1, &len);

    if (arg && (strncmp(arg, "off", 3) == 0)) {
        tickless_exit();
        wifi_mgmr_sta_ps_exit();
        snprintf(out, outlen, "tickless disabled (WiFi PS off)\r\n");
        return pdFALSE;
    }

    int dtim = arg ? atoi(arg) : 1;
    if (dtim < 1) {
        dtim = 1;   /* RC32K: keep DTIM small to bound wake-time drift */
    }
    lpfw_cfg.dtim_origin = (uint8_t)dtim;
    lpfw_cfg.tim_wakeup_en = 1;
    bl_lp_fw_bcn_loss_cfg_dtim_default(lpfw_cfg.dtim_origin);

    int ps = wifi_mgmr_sta_ps_enter();   /* put the WiFi driver into DTIM power-save */
    tickless_enter();                    /* enable_tickless=1: idle task may now enter PDS15 */

    snprintf(out, outlen, "tickless enabled: dtim=%d ps_enter=%d (idle -> PDS15, DTIM wake)\r\n", dtim, ps);
    return pdFALSE;
}

void lp_cli_register(void)
{
    static const CLI_Command_Definition_t lp_def = {
        "lpsleep",
        "lpsleep: enter PDS15 via lp_fw (WiFi-retention path; enters, resume-wake incomplete)\r\n",
        cmd_lpsleep,
        0
    };
    FreeRTOS_CLIRegisterCommand(&lp_def);

    static const CLI_Command_Definition_t pds_def = {
        "pdssleep",
        "pdssleep: bare PDS15 sleep; PDS timer wakes (resets the chip) after ~5 s\r\n",
        cmd_pdssleep,
        0
    };
    FreeRTOS_CLIRegisterCommand(&pds_def);

    static const CLI_Command_Definition_t tl_def = {
        "tickless",
        "tickless <dtim>|off: arm tickless-idle PDS15 (WiFi-retention DTIM wake); dtim default 1\r\n",
        cmd_tickless,
        -1   /* variable parameters (0 or 1) */
    };
    FreeRTOS_CLIRegisterCommand(&tl_def);
}
