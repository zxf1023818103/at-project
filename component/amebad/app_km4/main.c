/*
 * AmebaD KM4 (HP) image2 — minimal "basic AT log" application.
 *
 * Driver-only bootstrap: instead of the SDK example app (wifi + mbedTLS + the
 * example_entry tree), this is a minimal main that brings up just the basic AT
 * log / shell console over UART and starts the FreeRTOS scheduler. WiFi, BT and
 * mbedTLS are intentionally NOT pulled in (see component/amebad/config and
 * at_cmd — CONFIG_WLAN/BT forced off, only atcmd_sys registered).
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
#include "wifi_conf.h"   /* wifi_on / wifi_scan_networks + rtw_* scan structs */
#include "netconf.h"     /* LwIP_Init */

/* basic AT log / shell console (monitor shell over the log UART) */
extern void shell_init_rom(u32 TBLSz, void *pTBL);
extern void shell_init_ram(void);
extern void shell_uart_irq_rom(void *Data);

/* ---- WiFi bring-up (scan milestone) ------------------------------------- *
 * Prove the KM4 host driver + KM0 WiFi FW + dual-core IPC handshake are all
 * live by running LwIP_Init -> wifi_on(STA) -> wifi_scan_networks and printing
 * the AP list. Connect / DHCP / AT commands are follow-on work. Mirrors the
 * SDK's wlan_network()/init_thread but trimmed (no interactive mode).         */

/* Called once per AP found, then once more with scan_complete = RTW_TRUE.
 * NB: DiagPrintf is Realtek's limited ROM printf — it does NOT support
 * space-padded width specifiers like %2u / %4d (only %0Nx-style). Feeding it
 * those mis-parses the format, shifts vararg consumption, and a following %s
 * then dereferences a garbage pointer -> fault. Stick to plain %d / %x / %s. */
static rtw_result_t wifi_scan_print(rtw_scan_handler_result_t *malloced_scan_result)
{
    if (malloced_scan_result->scan_complete != RTW_TRUE) {
        rtw_scan_result_t *ap = &malloced_scan_result->ap_details;
        char ssid[33];
        int n = ap->SSID.len;
        if (n < 0 || n > 32) n = 0;
        for (int i = 0; i < n; i++) ssid[i] = (char)ap->SSID.val[i];
        ssid[n] = '\0';
        DiagPrintf("[KM4][scan] %02x:%02x:%02x:%02x:%02x:%02x  ch %d  %d dBm  %s\n\r",
                   ap->BSSID.octet[0], ap->BSSID.octet[1], ap->BSSID.octet[2],
                   ap->BSSID.octet[3], ap->BSSID.octet[4], ap->BSSID.octet[5],
                   (int)ap->channel, (int)ap->signal_strength, ssid);
    } else {
        DiagPrintf("[KM4][scan] ==== scan complete ====\n\r");
    }
    return RTW_SUCCESS;
}

static void wifi_bringup_thread(void *param)
{
    (void)param;

    DiagPrintf("[KM4] LwIP_Init()\n\r");
    LwIP_Init();

    DiagPrintf("[KM4] wifi_on(RTW_MODE_STA)...\n\r");
    if (wifi_on(RTW_MODE_STA) < 0) {
        DiagPrintf("[KM4] wifi_on FAILED\n\r");
        vTaskDelete(NULL);
        return;
    }
    DiagPrintf("[KM4] wifi_on OK (heap 0x%x); scanning...\n\r",
               (unsigned int)xPortGetFreeHeapSize());

    if (wifi_scan_networks(wifi_scan_print, NULL) != RTW_SUCCESS)
        DiagPrintf("[KM4] wifi_scan_networks: failed to start\n\r");

    vTaskDelete(NULL);
}

int main(void)
{
    DiagPrintf("\n\r[KM4] at-project image2 — WiFi scan bring-up\n\r");
    DiagPrintf("[KM4] FreeRTOS kernel %s\n\r", tskKERNEL_VERSION_NUMBER);

    /* Bring up the AT log / shell console. Mirrors the SDK example main's
       console init: ROM shell + RAM shell, then register and enable the LOGUART
       RX ISR (shell_uart_irq_rom) so typed commands are captured. */
    shell_init_rom(0, 0);
    shell_init_ram();
    InterruptRegister((IRQ_FUN)shell_uart_irq_rom, UART_LOG_IRQ, (u32)NULL, 5);
    InterruptEn(UART_LOG_IRQ, 5);

    /* WiFi bring-up runs as its own task (wifi_on blocks up to ~20s waiting for
       the driver thread, so it must not be on the main/idle path). Stack sized
       like the SDK init_thread (1280 words). */
    if (xTaskCreate(wifi_bringup_thread, "wifi_bringup", (512 + 768), NULL,
                    tskIDLE_PRIORITY + 3, NULL) != pdPASS)
        DiagPrintf("[KM4] xTaskCreate(wifi_bringup) failed\n\r");

    /* Run. Console / shell + the bring-up task run as FreeRTOS tasks. */
    vTaskStartScheduler();

    for (;;) {
    }
}
