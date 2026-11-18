# AmebaGreen2 硬件组件

目录按 `component/amebadplus` / `component/amebad` 的组件归属提取。构建只使用本目录、`project/amebagreen2` 和共享的 `component/*_config`，不需要 `platform/ameba-rtos`。项目仍包含 Realtek SDK 的驱动、头文件和预编译库；移除的是对原 SDK 目录的依赖。

| 目录 | 内容 |
|---|---|
| `fwlib`、`mbed`、`usrcfg` | Green2 寄存器、RAM 驱动、HAL、启动/Flash/电源配置 |
| `cmsis_rom`、`string_rom`、`dlist`、`platform_stdlib` | 芯片 CMSIS、ROM 和平台接口 |
| `osdep`、`freertos_port`、`pmu` | FreeRTOS 11、TLSF、OS wrapper、分核 PMC |
| `monitor_port`、`syscall_port` | COM15 控制台、RTC、诊断接口 |
| `wifi`、`wlan`、`wpa_supplicant` | Wi-Fi、双核 IPC/共存、WPS/EAP 和厂商库 |
| `freertos_tcp_port` | FreeRTOS+TCP 适配及无线库所需的 lwIP ABI 头文件；不编译 lwIP 协议栈 |
| `hci`、`nimble_port` | Green2 控制器补丁、HCI/DTM、共享 NimBLE 的传输适配 |
| `mbedtls_port` | ROM/IPSEC AES-GCM、硬件熵及已知答案测试 |
| `lfs_port`、`ota_flash_port` | LittleFS、双槽 OTA、加载记录验证 |
| `board_flash_port`、`board_dsleep_port`、`efuse_port` | Flash 保护、SleepCG、默认关闭的 OTP 调试接口 |
| `image_tool` | 本地镜像打包、签名/加密校验及串口下载器 |

KM4NS 无线固件为 `project/amebagreen2/app_km4ns/lib_wifi_fw.a`；KM4TZ 启动库为 `project/amebagreen2/bootloader_km4tz/lib_bootloader.a`。不同核心的同名驱动分别位于 `fwlib/km4ns` 和 `fwlib/km4tz`，不能互换。

来源提交、逐文件路径和原始/本地 SHA-256 记录在 [UPSTREAM.json](UPSTREAM.json)。原版权声明保留，SDK 顶层许可在 `LICENSE.realtek`。本地适配包括 include 路径、FreeRTOS 版本接口、Wi-Fi 连接状态回调、镜像布局、Green2 ROM ABI、缓存维护和安全域接口。
