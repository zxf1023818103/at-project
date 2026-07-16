/* lp_recovery.c — the bl_lp sys-exit (wake) callback for the LP_FW tickless path.
 *
 * The SDK contract for the minimal tickless.c (the one we compile) is that the APP registers the
 * post-wake restore via bl_lp_sys_callback_register(): tickless.c's lp_hook_post_sys only runs
 * bl_lp_call_sys_after_exit() + re-enables the mtimer IRQ, and bl_lp_fw_enter's resume restores
 * CPU state but NOT the board. Every SDK lp demo (sdio_wifi/uart_wifi app_pm.c lp_exit,
 * wl_ble_lp main.c lp_exit) registers this callback; our integration never did — so the first
 * real PDS15 resume came back to a dead board (no clocks/console/tick re-init) and the device
 * looked like it never woke.
 *
 * Mirrors the demo lp_exit set, with our equivalents:
 *   board_recovery()        — system_clock_init (WIFIPLL 320M + CPU_Set_MTimer_CLK) +
 *                             peripheral_clock_init_lp + console_init  (board.c)
 *   board_rf_ctl(RESET)     — RF restore (board_rf.c), same call the demos make
 *   vPortSetupTimerInterrupt() — re-arm the FreeRTOS mtimer tick (mtimecmp is lost in PDS)
 *   bflb_console_rx_reattach() — console RX IRQ enable + int-unmask (syscall_port), lost in PDS
 */

#include "FreeRTOS.h"
#include "task.h"

#include "bl_lp.h"
#include "board.h"
#include "board_rf.h"

extern void vPortSetupTimerInterrupt(void);
extern void bflb_console_rx_reattach(void);

/* The whole PDS15 resume runs ON THE IDLE TASK STACK (vApplicationSleep -> bl_lp_fw_enter -> the
 * sys-exit callback below -> board_recovery + wl_init RF re-init). The image's default
 * vApplicationGetIdleTaskMemory is a WEAK vendor implementation inside the prebuilt
 * libmacsw_os_adapter.a (rtos_al.c) sized to the vendor's own tiny minimal-stack config — the
 * first real resume overflows it (caught live on JTAG: vTaskSwitchContext ->
 * vApplicationStackOverflowHook spin). Every SDK lp demo overrides it with a 1024-word static
 * stack in main.c (wl_ble_lp main.c:151); do the same for LP builds. */
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   configSTACK_DEPTH_TYPE *puxIdleTaskStackSize)
{
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[1024];

    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *puxIdleTaskStackSize = 1024;
}

static int lp_sys_exit(void *arg)
{
    (void)arg;

    board_recovery();
    board_rf_ctl(BRD_CTL_RF_RESET_DEFAULT, 0);
    vPortSetupTimerInterrupt();
    bflb_console_rx_reattach();

    return 0;
}

void lp_recovery_register(void)
{
    bl_lp_sys_callback_register(NULL, NULL, lp_sys_exit, NULL);
}
