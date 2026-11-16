#include "ameba_soc.h"
#include "main.h"
#include "os_wrapper.h"
#if defined(CONFIG_BT_COEXIST)
#include "rtw_coex_ipc.h"
#endif
#include "ameba_diagnose.h"

static const char *const TAG = "MAIN";

#if (defined(CONFIG_BT) && CONFIG_BT) && (defined(CONFIG_BT_INIC) && CONFIG_BT_INIC)
#include "bt_inic.h"
#endif

void app_pmu_init(void)
{
	pmu_acquire_wakelock(PMU_OS);
	pmu_acquire_wakelock(PMU_CPU0_RUN);
	pmu_init_wakeup_timer();
}

void app_IWDG_refresh(void *arg)
{
	UNUSED(arg);
	WDG_Refresh(IWDG_DEV);
}

void app_IWDG_init(void)
{
	if (!(HAL_READ32(SYSTEM_CTRL_BASE, REG_AON_FEN) & APBPeriph_IWDG)) {
		return;
	}
	IWDG_LP_Enable(IWDG_DEV, DISABLE);
	WDG_Timeout(IWDG_DEV, 0x0FFF);
	WDG_Refresh(IWDG_DEV);
	RTK_LOGI(TAG, "IWDG refresh on!\n");

	/* Due to inaccurate of Aon clk(50% precision), IWDG should be refreshed every 2S with the lowest priority level thread. */
	/* Writing to FLASH during OTA makes SYSTICK stop and run frequently,
	and SYSTICK may be 3-4 times slower than expected, so reduce refresh period to 1/4 of 2s. */
	rtos_timer_t xTimer = NULL;
	rtos_timer_create(&xTimer, "WDG_Timer", NULL, 500, TRUE, app_IWDG_refresh);

	if (xTimer == NULL) {
		RTK_LOGE(TAG, "IWDG refresh error\n");
	} else {
		rtos_timer_start(xTimer, 0);
	}
}

_WEAK void app_example(void)
{

}

//default main
int main(void)
{
	/* Debug log control */
	rtk_log_level_set("*", RTK_LOG_INFO);
#ifdef AT_GREEN2_HCI
    /* Both cores share the H4 UART. Suppress asynchronous firmware logs. */
    rtk_log_level_set("*", RTK_LOG_NONE);
#endif

	InterruptRegister((IRQ_FUN)IPC_INTHandler, IPC_CPU1_IRQ, (u32)IPCNP_DEV, INT_PRI_MIDDLE);
	InterruptEn(IPC_CPU1_IRQ, INT_PRI_MIDDLE);

	/*IPC table initialization*/
	ipc_table_init(IPCNP_DEV);

	app_pmu_init();
    /* The application console is owned by KM4TZ, not the ROM shell. */
    InterruptDis(UART_LOG_IRQ);
    InterruptDis(GPIOB_IRQ);

#if defined(CONFIG_WIFI_FW_EN) && defined(CONFIG_FW_DRIVER_COEXIST)
	wififw_task_create();
#endif

#if defined(CONFIG_BT_COEXIST)
	/* init coex ipc */
	coex_ipc_entry();
#endif

#ifdef CONFIG_WLAN
	wifi_init();
#endif

	/* initialize BT iNIC */
#if (defined(CONFIG_BT) && CONFIG_BT) && (defined(CONFIG_BT_INIC) && CONFIG_BT_INIC)
	bt_inic_init();
#endif

#ifdef CONFIG_SHELL
	shell_init_rom(0, NULL);
	shell_init_ram();
	/* Register Log Uart Callback function */
	InterruptRegister((IRQ_FUN) shell_uart_irq_rom, UART_LOG_IRQ, (u32)NULL, INT_PRI_LOWEST);
	InterruptEn(UART_LOG_IRQ, INT_PRI_LOWEST);
	LOGUART_INTCoreConfig(LOGUART_DEV, LOGUART_BIT_INTR_MASK_KM4NS, ENABLE);
#endif

	app_IWDG_init();



	/* Execute application example */
	app_example();

	IPC_patch_function(&rtos_critical_enter, &rtos_critical_exit);
	IPC_SEMDelayStub(DelayMs);

	RTK_LOGI(TAG, "KM4NS OS START \n");

	//Enable Schedule
	rtos_sched_start();
}
