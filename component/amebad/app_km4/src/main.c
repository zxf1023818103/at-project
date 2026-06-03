/*
 * AmebaD KM4 (HP) image2 — minimal "basic AT log" application.
 *
 * Driver-only bootstrap: instead of the SDK example app (wifi + mbedTLS + the
 * example_entry tree), this is a minimal main that brings up just the basic AT
 * log / shell console over UART and starts the FreeRTOS scheduler. WiFi, BT and
 * mbedTLS are intentionally NOT pulled in (see component/amebad/config_km4 and
 * at_cmd_km4 — CONFIG_WLAN/BT forced off, only atcmd_sys registered).
 *
 * By the time KM4 main() runs, the KM0 bootloader + KM4 app_start have set up
 * clocks / PSRAM / flash and handed console control to KM4, so main() only has
 * to start the console and the scheduler.
 *
 * The SDK's full example main is preserved in the submodule at
 * project/realtek_amebaD_va0_example/src/src_hp/main.c.
 */
#include "ameba_soc.h"
#include "FreeRTOS.h"
#include "task.h"

/* basic AT log / shell console (monitor shell over the log UART) */
extern void shell_init_rom(u32 TBLSz, void *pTBL);
extern void shell_init_ram(void);
extern void shell_uart_irq_rom(void *Data);

int main(void)
{
    DiagPrintf("\n\r[KM4] at-project minimal image2 — basic AT log\n\r");
    DiagPrintf("[KM4] FreeRTOS kernel %s\n\r", tskKERNEL_VERSION_NUMBER);

    /* Bring up the AT log / shell console (no WiFi / BT). Mirrors the SDK
       example main's console init: ROM shell + RAM shell, then register and
       enable the LOGUART RX ISR (shell_uart_irq_rom) so typed commands are
       actually captured — shell_init_ram() alone enables the IRQ but doesn't
       register the handler, so without this the console is output-only. */
    shell_init_rom(0, 0);
    shell_init_ram();
    InterruptRegister((IRQ_FUN)shell_uart_irq_rom, UART_LOG_IRQ, (u32)NULL, 5);
    InterruptEn(UART_LOG_IRQ, 5);

    /* Run. The console / shell runs as FreeRTOS task(s) from here. */
    vTaskStartScheduler();

    for (;;) {
    }
}
