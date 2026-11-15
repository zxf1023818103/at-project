# AmebaDplus 硬件组件

目录按 `component/amebad` 的组件归属组织。AmebaDplus 构建使用本目录和 `project/amebadplus` 中的文件，不依赖外部 SDK 目录。项目仍包含来自 Realtek SDK 的驱动、头文件和预编译库；移除的是原 SDK 目录依赖。

| 目录 | 内容 |
|---|---|
| `fwlib` | 芯片寄存器、RAM 驱动、芯片信息库 |
| `mbed` | GPIO、串口、Flash 等 HAL 接口 |
| `usrcfg` | 启动、Flash、电源、Wi-Fi 配置 |
| `cmsis_rom`、`string_rom` | CMSIS 核心头文件、ROM 字符串及平台函数接口 |
| `dlist`、`platform_stdlib` | 链表和平台标准库声明 |
| `osdep`、`pmu` | OS wrapper、堆适配、电源管理和分核 PMC 库 |
| `monitor_port` | 控制台适配、诊断头文件、分核 crashdump 库 |
| `wifi`、`wlan` | Wi-Fi API、IPC/共存适配及无线厂商库 |
| `wpa_supplicant` | WPA/WPS/EAP 的接口、配置、TLS 适配和厂商库 |
| `hci` | 蓝牙控制器补丁、传输和控制器初始化 |
| `freertos_tcp_port` | FreeRTOS+TCP 适配及厂商接口所需的 lwIP ABI 头文件；不编译 lwIP 协议栈 |
| `image_tool` | 本地镜像打包、签名加密、校验和 ImageTool 下载工具 |

KM0 无线固件库位于 `project/amebadplus/app_km0/lib_wifi_fw.a`，KM4 启动库位于 `project/amebadplus/bootloader_km4/lib_bootloader.a`；分核同名库以 `_km0` / `_km4` 后缀区分。

`UPSTREAM.json` 记录本次提取的 SDK 提交、503 个文件的来源路径及原始/本地 SHA-256。原文件的版权和许可声明保留，SDK 顶层许可证保存在 `LICENSE.realtek`，lwIP 的许可证随兼容头文件保存；具体文件仍按各自声明处理。

本地调整包括扁平化目录后的 include 路径，以及打包器从明确指定的项目目录读取 SoC、manifest 和内存布局。打包器不再扫描 SDK 或读取 SDK 的 SoC 映射；签名和 RSIP 格式保留原实现，并由独立校验代码检查。
