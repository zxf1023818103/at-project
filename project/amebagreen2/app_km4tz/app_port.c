#include "ameba_soc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"
#include "os_wrapper.h"
#include "app_startup.h"
#include "wifi_service.h"
#include "log_lock.h"
#include "board_flash_lock.h"
#if defined(AT_PROJECT_NIMBLE) || defined(AT_PROJECT_HCI)
#include "rtw_coex_ipc.h"
#endif
#include <stdio.h>

extern void mbedtls_threading_freertos_init(void);
extern void net_cli_register(void);
extern void wifi_cli_register(void);
extern void sntp_sync_start(void);
static volatile int app_ready;
int app_ota_selftest_ready(void) { return app_ready; }

static BaseType_t reboot_cmd(char *out, size_t len, const char *cmd)
{
    (void)out; (void)len; (void)cmd;
    System_Reset();
    return pdFALSE;
}
static const CLI_Command_Definition_t reboot_def = {
    "reboot", "reboot: reset both cores\r\n", reboot_cmd, 0
};

void app_platform_boot_init(void)
{
    InterruptRegister(IPC_INTHandler, IPC_CPU0_IRQ, (u32)IPCAP_DEV, INT_PRI5);
    InterruptEn(IPC_CPU0_IRQ, INT_PRI5);
    ipc_table_init(IPCAP_DEV);
    IPC_patch_function(&rtos_critical_enter, &rtos_critical_exit);
    IPC_SEMDelayStub(DelayMs);
    pmu_acquire_wakelock(PMU_OS);
    pmu_set_sleep_type(SLEEP_CG);
    pmu_init_wakeup_timer();
    SOCPS_SleepInit();
    /* SleepInit installs the SDK shell's second RX path on PB4. This project
     * owns LOGUART, so its GPIO interrupt must remain masked. */
    GPIO_INTConfig(_PB_4, DISABLE);
    InterruptDis(GPIOB_IRQ);
#if defined(AT_PROJECT_NIMBLE) || defined(AT_PROJECT_HCI)
    coex_ipc_entry();
#endif
    log_lock_init();
    mbedtls_threading_freertos_init();
#ifdef AT_ENABLE_TFM
    extern void green2_tfm_client_init(void);
    green2_tfm_client_init();
#endif
    printf("[AmebaGreen2] FreeRTOS %s / shared TLSF / FreeRTOS+TCP\n", tskKERNEL_VERSION_NUMBER);
}

void app_console_register_platform(void)
{
    FreeRTOS_CLIRegisterCommand(&reboot_def);
    extern void dsleep_cli_register(void);
    dsleep_cli_register();
    extern void flash_cli_register(void);
    flash_cli_register();
#ifdef AT_ENABLE_TFM
    extern void green2_tfm_status_register(void);
    green2_tfm_status_register();
#endif
#ifdef AT_EFUSE_DEBUG
    extern void efuse_burn_cli_register(void);
    efuse_burn_cli_register();
#endif
    wifi_cli_register();
#ifdef AT_WIFI_PROVISION
    extern void wifi_provision_cli_register(void);
    wifi_provision_cli_register();
#endif
#ifdef AT_PROJECT_NIMBLE
    extern void ble_tp_cli_register(void);
    ble_tp_cli_register();
#endif
#ifdef AT_PROJECT_AWS
    extern void aws_cli_register(void);
    extern void ota_http_register(void);
    aws_cli_register();
    ota_http_register();
#endif
    net_cli_register();
#ifdef AT_PROJECT_IPERF
    extern void iperf_cli_register(void);
    iperf_cli_register();
#endif
#ifdef AT_PROJECT_MINIZIP
    extern void zip_cli_register(void);
    zip_cli_register();
#endif
#ifdef AT_CRYPTO_SELFTESTS
    extern void bench_cli_register(void), crt_bundle_cli_register(void), pkcs11_cli_register(void);
    bench_cli_register(); crt_bundle_cli_register(); pkcs11_cli_register();
#endif
}

#ifdef AT_PROJECT_NIMBLE
static void ble_task(void *arg)
{
    (void)arg;
    extern void nimble_start(void);
    /* The controller RF calibration uses the WLAN/coexistence IPC service. */
    extern int amebagreen2_wifi_ready(void);
    while (!amebagreen2_wifi_ready()) vTaskDelay(pdMS_TO_TICKS(50));
    nimble_start();
    vTaskDelete(NULL);
}
#endif
static void ready_task(void *arg)
{
    (void)arg;
    /* HCI owns the UART and has no CLI to unlock before the next download. */
#ifdef AT_PROJECT_HCI
    configASSERT(board_flash_protect(BOARD_FLASH_UNLOCK,NULL,NULL,NULL,NULL));
#else
    /* Apply the AmebaD bottom-3-MiB lock; LFS/PSA remain writable. */
    configASSERT(board_flash_protect(BOARD_FLASH_LOCK_FW,NULL,NULL,NULL,NULL));
#endif
    board_flash_log_status();
#ifdef AT_ENABLE_TFM
    extern void green2_tfm_selftest(void);
    green2_tfm_selftest();
#endif
#ifdef AT_GREEN2_CRYPTO_KAT
    extern void amebagreen2_crypto_kat(void);
    amebagreen2_crypto_kat();
#endif
#ifdef AT_PROJECT_PKCS11
    extern void pkcs11_selftest_run(void);
    pkcs11_selftest_run();
#endif
#ifdef AT_PROJECT_CRYPTOBENCH
    extern void mbedtls_bench(void);
    mbedtls_bench();
#endif
#ifdef AMEBAD_CPP_SELFTEST
    extern void cpp_selftest(void);
    cpp_selftest();
#endif
#ifdef AT_PROJECT_VFS_SELFTEST
    extern int amebagreen2_wifi_ready(void);
    extern void vfs_selftest(void);
    while (!amebagreen2_wifi_ready()) vTaskDelay(pdMS_TO_TICKS(50));
    vfs_selftest();
#endif
    app_ready = 1;
    printf("[AmebaGreen2] application ready\n");
    vTaskDelete(NULL);
}
void app_platform_wifi_bringup(void) { wifi_service_start(); }
void app_platform_pre_scheduler_services(void)
{
#ifdef AT_PROJECT_HCI
    extern void amebagreen2_hci_start(void);
    amebagreen2_hci_start();
#endif
#ifdef AT_PROJECT_NIMBLE
    configASSERT(xTaskCreate(ble_task, "ble_boot", 3072, NULL, 2, NULL) == pdPASS);
#endif
    configASSERT(xTaskCreate(ready_task, "ready", 4096, NULL, 2, NULL) == pdPASS);
}
void app_network_up_start_services(void)
{
    sntp_sync_start();
#ifdef AT_ENABLE_QUIC
    extern void quic_test_start(void);
    quic_test_start();
#endif
#ifdef AT_PROJECT_AWS_OTA
    extern void aws_iot_demo_start(void);
    aws_iot_demo_start();
#elif defined(AT_PROJECT_AWS)
    extern void aws_telemetry_start(void);
    aws_telemetry_start();
#endif
}
