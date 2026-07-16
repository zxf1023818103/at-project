#pragma once

#ifdef __cplusplus
static_assert(__cplusplus >= 201703L, "at-project requires C++17 or later");
#endif

// Shared prelude for the Bouffalo + Ameba-newer platform bodies in main.cpp. The AmebaD
// (CONFIG_PLATFORM_8721D/8710C) branch is self-contained and never includes this file.
//
// 2026-07 cleanup: the historical BLE scaffolding died with the Zephyr BT host removal and the
// NimBLE migration, and a repo-wide reference scan confirmed it all unused — the RTK BT stack
// headers (os_*/gap_*/bte/profile_server), the Zephyr bt_le_adv_* shim declarations, the
// API_TASK_*/RTK_BT_* task macros (two identical copies), HI_WORD/LO_WORD, WIFI_STACK_SIZE/
// TASK_PRIORITY_FW, the whole Zephyr BT_DATA_*/BT_GAP_*/BT_LE_ADV_*/BT_LE_SCAN_* AD/GAP macro
// block, and the at:: std-alias/type-trait prelude. All deleted.

extern "C" {

#include <FreeRTOS.h>
#include <string.h>
#include <task.h>
#include <semphr.h>

// Bouffalo has switched its network stack to FreeRTOS-Plus-TCP; the lwIP headers
// no longer exist there. The ameba platforms still run lwIP, so keep the include
// for them. (The Bouffalo chip set: BL602/BL702/BL606P/BL808/BL616/BL616CL.)
#if !defined(BL602) && !defined(BL702) && !defined(BL606P) && !defined(BL808) && !defined(BL616) && !defined(BL616CL)
#include <lwip/tcpip.h>
#endif

#if defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2
#include <wifi_api.h>
#include <bt_api_config.h>
#include <rtk_stack_config.h>
#include <wifi_fast_connect.h>
#endif // defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2

#if defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616 || defined BL616CL
// KV/NV is now POSIX env (setenv/getenv, persisted to /KV littlefs files by
// component/syscall_config/env.c) — same as AmebaD. The Bouffalo-specific
// lfs_easyflash (<easyflash.h>) was removed; these two hooks replace easyflash_init.
#include <wifi_mgmr_ext.h>
extern "C" void *vfs_lfs_mount(void);   // lfs_port: idempotent PSM LittleFS mount
extern "C" void  env_init(void);        // syscall_config/env.c: load /KV files into environ
// Interactive FreeRTOS-Plus-CLI console (component/monitor_config) — started from main() on
// uart0; same command set as AmebaD. AtTask keeps uart1 (separate UART, no RX contention).
extern "C" {
    void monitor_start(void);                  // shared engine: version/heap/tasks/echo + line editor
    void bouffalo_monitor_cmds_register(void); // Bouffalo port: reboot
    void fs_cli_register(void);                 // ls/cat/rm/mkdir/mv/cp/touch/hexdump/df/mkfs/chmod + hashes
    void env_cli_register(void);                // setenv/getenv/env/unsetenv
    void time_cli_register(void);               // date/uptime/sntp_update
    void sntp_sync_start(void);                 // coreSNTP UTC time sync (component/sntp_time_config)
    void zip_cli_register(void);                // zip/unzip (component/monitor_config zip_cmd -> minizip)
    void log_lock_init(void);                   // rxi logger FreeRTOS-mutex lock (component/rxi_log_config)
    void iperf_cli_register(void);              // `iperf [server|tx|udp]` throughput command (component/iperf_config)
    void bench_cli_register(void);              // `bench` Mbed TLS crypto microbenchmark (component/mbedtls_bench_config)
    void crt_bundle_cli_register(void);         // `crt_bundle` Mozilla CA root bundle command (component/esp_crt_bundle_config)
    void pkcs11_cli_register(void);             // `pkcs11` corePKCS11 token self-test command (component/pkcs11_selftest_config)
}
#endif // defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616 || defined BL616CL

#if defined BL602 || defined BL702
// (async_event.h removed: WiFi events dispatch directly via the wifi4 adapter's platform_post_event;
//  main.cpp registers with platform_register_event -- platform_al.h comes via the wifi4 SDK headers.)
#include <timers.h>
#include <bflb_irq.h>
#include <bflb_uart.h>
#include <bflb_gpio.h>
#include <board.h>
#include <bflb_mtd.h>
#include <rfparam_adapter.h>
#if defined(CONFIG_BLUETOOTH)
#include <ble_lib_api.h>
#endif
#endif // defined BL602 || defined BL702

#if defined BL606P || defined BL808 || defined BL616 || defined BL616CL
#include <wifi_mgmr.h>
#include <bl_fw_api.h>
#include <bflb_irq.h>
#if defined BL616CL
#include <bl616cl_glb.h>
#else
#include <bl616_glb.h>
#endif
#include <rfparam_adapter.h>
#include <board.h>
#include <bflb_mtd.h>
#include <btble_lib_api.h>
#endif // defined BL606P || defined BL808 || defined BL616 || defined BL616CL

}  // extern "C"

#include <cstdio>

#define my_assert(x) do { if (!(x)) { ::std::printf("Assertion failed: %s, %s, %d\n", #x, __FILE__, __LINE__); while(1); } } while(0)
