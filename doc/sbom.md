# SBOM (Software Bill of Materials)

生成时间：2026-05-23

扫描提交（各 SDK 子模块 HEAD）：

| SDK | 子模块路径 | HEAD SHA |
|---|---|---|
| bouffalo_sdk | `platform/bouffalo_sdk` | `a7b19019ef282b3d06b23edad3376f3e1a67a35e` |
| ameba-rtos-d | `platform/ameba-rtos-d` | `23657427255773635de7d16497c03ac32d36de37` |
| ameba-rtos    | `platform/ameba-rtos`   | `0e9051318a7d0ed7c773f800f31acd010a29343e` |

下面所有「路径」列均为相对于 `/home/zxf/at-project/` 的相对路径。芯片厂自有源码（bflb_/bl_/rtl_/ameba_ 前缀的 HAL、BLE controller binary、WiFi/BT 私有协议栈、IPC 库等）不在此 SBOM 范围。

## 摘要

| SDK | 列入组件数 | 主要 license | 备注 |
|---|---|---|---|
| bouffalo_sdk | 22 | MIT / BSD-3-Clause / Apache-2.0 | 巨型 SDK；多媒体大部分是私有 .a，仅本表列开源源码 |
| ameba-rtos-d | 9  | MIT / BSD / Apache-2.0 / EPL-1.0 | 较老的 AmebaD SDK |
| ameba-rtos   | 24 | MIT / BSD / Apache-2.0 / GPL / RPSL | 新一代 Ameba SDK，包含 AI / 多媒体 / ATF |

---

## bouffalo_sdk

| 组件 | 版本 | License | 路径 | 用途 |
|---|---|---|---|---|
| FreeRTOS Kernel | 10.6.2 | MIT | `platform/bouffalo_sdk/components/os/freertos/` | RTOS |
| FreeRTOS POSIX wrapper | upstream `FreeRTOS-POSIX` | MIT | `platform/bouffalo_sdk/components/os/freertos/posix/` | POSIX 兼容层 |
| lwIP | 2.1.2 | BSD-3-Clause | `platform/bouffalo_sdk/components/net/lwip/lwip/` | TCP/IP 栈 |
| mbedTLS (v2 LTS) | 2.28.2 | Apache-2.0 | `platform/bouffalo_sdk/components/crypto/mbedtls/mbedtls/` | TLS / 加密 |
| mbedTLS (v3) | 3.6.5 | Apache-2.0 | `platform/bouffalo_sdk/components/crypto/mbedtls/mbedtls_v3/` | TLS / 加密 |
| wpa_supplicant + hostapd (Wi-Fi 6) | 2.10 | BSD-3-Clause | `platform/bouffalo_sdk/components/wireless/wifi6/wpa_supplicant/` | WPA/WPA2/WPA3 |
| wpa_supplicant + hostapd (Wi-Fi 4) | ? (~2.x) | BSD-3-Clause | `platform/bouffalo_sdk/components/wireless/wifi4/wpa_supplicant/` | WPA 旧版 |
| bl_wpa_supplicant | ? (~2.x) | BSD-3-Clause | `platform/bouffalo_sdk/components/wireless/bl_wpa_supplicant/` | BL602 旧分支 wpa |
| OpenThread | API 512 (~2024.x) | BSD-3-Clause | `platform/bouffalo_sdk/components/wireless/thread/openthread/` | 802.15.4 Thread |
| Zephyr BLE host + Mesh | 基于 Zephyr (Nordic/Intel 移植) | Apache-2.0 | `platform/bouffalo_sdk/components/wireless/bluetooth/blestack/`, `…/blemesh/` | BLE Host / Mesh |
| Bluedroid SBC (Android/Open Interface) | ? | Apache-2.0 | `platform/bouffalo_sdk/components/wireless/bluetooth/blestack/src/sbc/` | A2DP SBC 编解码 |
| littlefs | 2.8 (LFS_VERSION 0x00020008) | BSD-3-Clause | `platform/bouffalo_sdk/components/fs/littlefs/littlefs/` | Flash 文件系统 |
| FatFs (ChaN) | R0.15 w/patch3 | BSD-1-Clause (ChaN) | `platform/bouffalo_sdk/components/fs/fatfs/` | FAT 文件系统 |
| cJSON | upstream `324973008c…` | MIT | `platform/bouffalo_sdk/components/utils/cjson/` | JSON 解析 |
| Micro QuickJS (Bellard) | ? | MIT | `platform/bouffalo_sdk/components/utils/mquickjs/` | JS 引擎 |
| MQTT-C (LiamBindle) | ? | MIT | `platform/bouffalo_sdk/components/net/lib/mqtt/MQTT-C/` | MQTT client |
| Zephyr HTTP component | v4.1.0 (git 7823374) | Apache-2.0 | `platform/bouffalo_sdk/components/net/lib/http/` | HTTP client |
| libpeer (sepfy) | git `3c8a8be11a…` | MIT | `platform/bouffalo_sdk/components/net/lib/libpeer/` | WebRTC for IoT |
| libsrtp (Cisco) | ? | BSD-3-Clause | `platform/bouffalo_sdk/components/net/lib/srtp/libsrtp/` | SRTP |
| CherryUSB (sakumisu) | 1.5.3 | Apache-2.0 | `platform/bouffalo_sdk/components/usb/cherryusb/` | USB host/device |
| libmetal | 1.8.0 | BSD-3-Clause | `platform/bouffalo_sdk/components/ipc/libmetal/libmetal/` | OpenAMP HAL |
| OpenAMP | 1.8.0 | BSD-3-Clause | `platform/bouffalo_sdk/components/ipc/open-amp/open-amp/` | 多核 IPC |
| TLSF allocator (Matt Conte) | ? | BSD-3-Clause | `platform/bouffalo_sdk/components/mm/tlsf/` | 堆分配器 |
| xz Embedded (Lasse Collin) | upstream xz_embedded | Public Domain | `platform/bouffalo_sdk/components/lzma/xz/` | LZMA2/xz 解压 |
| LVGL | 8.4.0 / 9.5.0（两个版本共存） | MIT | `platform/bouffalo_sdk/components/graphics/lvgl_v8/`, `…/lvgl_v9/` | GUI |
| libfvad (WebRTC VAD) | ? | BSD-3-Clause | `platform/bouffalo_sdk/components/multimedia/libfvad/` | 语音活动检测 |
| blcrypto_suite | 基于 mbedTLS 2.16.11 | Apache-2.0 | `platform/bouffalo_sdk/components/wireless/wifi4/blcrypto_suite/` | wifi4 加密套件（mbedtls fork） |

注：`components/multimedia/` 下大部分子目录（opus / aacdec / vorbis / speex / pvmp3dec / amrnb / amrwb / sonic / xav / speexdsp / ogg）在本仓库里只有 `include/` 头文件 + `libs/*.a` 预编译产物，没有源码；它们对应的上游分别是 libopus (BSD)、Helix AAC (RPSL)、libvorbis (BSD)、Speex/SpeexDSP (BSD)、PVMP3Dec (Apache-2.0)、opencore-amrnb/wb (Apache-2.0)、libsonic (Apache-2.0) 等，license 见各项目上游；这里不再单独占行。`components/wireless/bluetooth/blecontroller`、`btblecontroller`、`btprofile` 为预编译 `.a`，属厂家私有。

## ameba-rtos-d

| 组件 | 版本 | License | 路径 | 用途 |
|---|---|---|---|---|
| FreeRTOS Kernel | 10.2.0 | MIT | `platform/ameba-rtos-d/component/os/freertos/freertos_v10.2.0/` | RTOS |
| lwIP | 2.0.3 (`lwip_v2.0.2` 目录但 init.h 显示 2.0.3) | BSD-3-Clause | `platform/ameba-rtos-d/component/common/network/lwip/lwip_v2.0.2/` | TCP/IP（旧） |
| lwIP | 2.2.0 | BSD-3-Clause | `platform/ameba-rtos-d/component/common/network/lwip/lwip_v2.2.0/` | TCP/IP（新） |
| mbedTLS | 2.4.0 / 3.2.1 / 3.4.0 / 3.6.0 共四份 | Apache-2.0 | `platform/ameba-rtos-d/component/common/network/ssl/mbedtls-*/` | TLS / 加密 |
| littlefs | 2.8 (LFS_VERSION 0x00020008) | BSD-3-Clause | `platform/ameba-rtos-d/component/common/file_system/littlefs/r2.80/` | Flash FS |
| FatFs (ChaN) | R0.10b / R0.15 共两份 | BSD-1-Clause (ChaN) | `platform/ameba-rtos-d/component/common/file_system/fatfs/r0.10c/`, `…/r0.15/` | FAT FS |
| cJSON | Dave Gamble (2009 vintage) | MIT | `platform/ameba-rtos-d/component/common/utilities/cJSON.[ch]` | JSON 解析 |
| Eclipse Paho Embedded MQTT (MQTTClient + MQTTPacket) | ? | EPL-1.0 / EDL-1.0 dual | `platform/ameba-rtos-d/component/common/application/mqtt/` | MQTT client |
| mbed CoAP (ARM `sn_coap`) | ? | Apache-2.0 | `platform/ameba-rtos-d/component/common/network/coap/` | CoAP |
| libcoap (Olaf Bergmann) – headers only | ? | BSD-2-Clause | `platform/ameba-rtos-d/component/common/network/libcoap/include/coap/` | CoAP（仅头文件） |

注：ameba-rtos-d 下 `httpc`、`httpd`、`mDNS`、`websocket`、`rtsp` 等子目录只剩 Realtek 的 wrapper / TLS 适配源（实现走 `lib_*.a`），所以不单列。CoAP 头文件目录里没有 `.c`，估计也是绑定到 `lib_*.a`。

## ameba-rtos

| 组件 | 版本 | License | 路径 | 用途 |
|---|---|---|---|---|
| FreeRTOS Kernel | 10.2.1 (SMP) / 10.4.3 / 10.5.1 共三份 | MIT | `platform/ameba-rtos/component/os/freertos/freertos_v10.*/` | RTOS（多份并存） |
| ARM Trusted Firmware (TF-A) | 2.3 (amebasmart 用) | BSD-3-Clause | `platform/ameba-rtos/component/soc/amebasmart/atf/` | AArch64 EL3 firmware |
| ATF-内嵌 mbedTLS | 2.16.2 | Apache-2.0 | `platform/ameba-rtos/component/soc/amebasmart/atf/mbedtls/` | ATF 镜像签名校验 |
| lwIP | 2.1.2 (Realtek 在 2.0.0 基础上 patch) | BSD-3-Clause | `platform/ameba-rtos/component/lwip/lwip_v2.1.2/` | TCP/IP |
| mbedTLS | 3.6.2 / 3.6.5 共两份 | Apache-2.0 | `platform/ameba-rtos/component/ssl/mbedtls-3.6.2/`, `…/mbedtls-3.6.5/` | TLS / 加密 |
| GmSSL | 3.1.1 | Apache-2.0 | `platform/ameba-rtos/component/ssl/gmssl-3.1.1/` | 国密 SM2/3/4 |
| littlefs | 2.8 | BSD-3-Clause | `platform/ameba-rtos/component/file_system/littlefs/r2.80/` | Flash FS |
| FatFs (ChaN) | R0.14b | BSD-1-Clause (ChaN) | `platform/ameba-rtos/component/file_system/fatfs/r0.14b/` | FAT FS |
| cJSON | Dave Gamble (2009-2017) | MIT | `platform/ameba-rtos/component/network/cJSON/` | JSON 解析 |
| Eclipse Paho Embedded MQTT | ? | EPL-1.0 / EDL-1.0 dual | `platform/ameba-rtos/component/network/mqtt/MQTTClient/`, `…/MQTTPacket/` | MQTT |
| nghttp2 | 1.31.0 | MIT | `platform/ameba-rtos/component/network/http2/nghttp2-1.31.0/` | HTTP/2 |
| mbed CoAP (ARM `sn_coap`) | ? | Apache-2.0 | `platform/ameba-rtos/component/network/coap/` | CoAP |
| libcoap headers | ? | BSD-2-Clause | `platform/ameba-rtos/component/network/libcoap/include/` | CoAP（仅头） |
| iperf3 (ESnet/LBNL) | ? | BSD-3-Clause (variant) | `platform/ameba-rtos/component/network/iperf3/` | 网络带宽测试 |
| dnrd (DNS Relay Daemon) – 仅 `.a` | ? | GPL-2.0-only (upstream) | `platform/ameba-rtos/component/network/dnrd/` (`lib_ipdnrd.a`) | DNS relay |
| libwsclient (etheriaa) | ? | MIT (upstream) | `platform/ameba-rtos/component/network/websocket/libwsclient.[ch]` | WebSocket client |
| LVGL | 8.3.11 / 9.3.0 共两份 | MIT | `platform/ameba-rtos/component/ui/LVGL/lvgl-8.3/`, `…/lvgl-9.3/` | GUI |
| zlib | 1.3 | Zlib | `platform/ameba-rtos/component/ui/third_party/zlib/` | 压缩 |
| libpng | 1.6.44 | libpng-2.0 (BSD-style) | `platform/ameba-rtos/component/ui/third_party/libpng/` | PNG |
| libjpeg-turbo | 1.5.3 | IJG / BSD-3-Clause / Zlib (tri-license) | `platform/ameba-rtos/component/ui/third_party/libjpeg-turbo/` | JPEG |
| TensorFlow Lite Micro | upstream port (~2024) | Apache-2.0 | `platform/ameba-rtos/component/tflite_micro/` | 边缘 ML |
| TFLite-micro 三方依赖：flatbuffers / gemmlowp / ruy | unknown | Apache-2.0 | `platform/ameba-rtos/component/tflite_micro/third_party/` | TFLite 依赖 |
| KissFFT | upstream (Mark Borgerding) | BSD-3-Clause | `platform/ameba-rtos/component/tflite_micro/third_party/kissfft/` | FFT |
| libFLAC | 1.3.4 | BSD-3-Clause (libFLAC) | `platform/ameba-rtos/component/audio/third_party/flac/` | FLAC codec |
| Helix AAC decoder (RealNetworks) | r1.1 (2005 vintage) | RPSL-1.0 / RCSL | `platform/ameba-rtos/component/audio/third_party/haac/` | AAC 解码 |
| libgsm (TU Berlin) | upstream (~1994) | TU-Berlin custom (free, non-OSI) | `platform/ameba-rtos/component/audio/third_party/libgsm/` | GSM 06.10 codec |
| libopus (Xiph) | ~1.3.x | BSD-3-Clause | `platform/ameba-rtos/component/audio/third_party/libopus/` | Opus codec |
| Tremolo (Vorbis fixed-point fork) | upstream | BSD-3-Clause | `platform/ameba-rtos/component/audio/third_party/tremolo/` | Vorbis 解码 |
| SpeexDSP | 1.2.1 | BSD-3-Clause | `platform/ameba-rtos/component/audio/third_party/speexdsp/` | 音频 DSP |

注：`component/usb/` 全部为 Realtek `Copyright (c) … Realtek Semiconductor Corp.` 私有 USB stack（host/device），未列入。`component/bluetooth/rtk_stack/` 同样是 Realtek 私有 BT stack。`platform/ameba-rtos/.venv/` 为 Python 构建工具虚拟环境，不属于固件代码，未列入。

## 共用组件交叉对比

| 组件 | bouffalo_sdk | ameba-rtos-d | ameba-rtos | license 一致性 |
|---|---|---|---|---|
| FreeRTOS | 10.6.2 | 10.2.0 | 10.2.1 / 10.4.3 / 10.5.1 | 全部 MIT，一致 |
| lwIP | 2.1.2 | 2.0.3 + 2.2.0 | 2.1.2（Realtek 加补丁） | 全部 BSD-3，一致 |
| mbedTLS | 2.28.2 + 3.6.5 | 2.4.0 + 3.2.1 + 3.4.0 + 3.6.0 | 3.6.2 + 3.6.5（外加 ATF 内嵌 2.16.2） | 全部 Apache-2.0，一致 |
| littlefs | 2.8 | 2.8 | 2.8 | 全部 BSD-3，一致；磁盘格式 LFS_DISK_VERSION 2.1 |
| FatFs | R0.15 | R0.10b + R0.15 | R0.14b | 全部 ChaN BSD-1-Clause，一致 |
| cJSON | DaveGamble 上游 | 09 年老版本 | 09–17 版本 | 全部 MIT，一致 |
| LVGL | 8.4 + 9.5 | – | 8.3.11 + 9.3 | 全部 MIT，一致 |
| MQTT 客户端 | MQTT-C (MIT) | Paho Embedded (EPL/EDL) | Paho Embedded (EPL/EDL) | 不一致：BL 用 MQTT-C，Ameba 系用 Paho |
| CoAP | （无） | ARM sn_coap + libcoap 头 | ARM sn_coap + libcoap 头 | 两 Ameba 一致 |

升级提示：
* 三个 SDK 的 mbedTLS 版本跨度大（从 2.4 到 3.6.5），若需统一安全基线，最低应升级到 3.6.x LTS。
* 三处 lwIP 都还在 2.x.x，整体可接受；ameba-rtos-d 的 2.0.3 路径明显落后于上游 2.2.x。
* FreeRTOS 不同 SDK 间版本差异较大（10.2 ↔ 10.6），跨平台代码的 portmacro / heap 接口需注意兼容。
* ameba-rtos 的 `haac` 是 RealNetworks **RPSL-1.0** copyleft 类许可证；`dnrd` 在上游是 **GPL-2.0-only**，目前以 `.a` 静态库方式链接，发货前请确认许可证义务。
* ameba-rtos 的 `libgsm` 是 TU-Berlin custom license（非 OSI 标准列表，但允许使用、要求保留 notice）。

## 来源说明 + 不确定项

明确写在 metadata 里的：
* FreeRTOS：`task.h` 中 `tskKERNEL_VERSION_NUMBER` 字符串。
* lwIP：`src/include/lwip/init.h` 中 `LWIP_VERSION_MAJOR/MINOR/REVISION`。
* mbedTLS：`mbedtls/build_info.h` 或 `version.h` 中 `MBEDTLS_VERSION_MAJOR/MINOR/PATCH`。
* littlefs：`lfs.h` 中 `LFS_VERSION` 宏。
* FatFs：`ff.c` 顶部注释中 `R0.xxx` 行。
* LVGL：`lvgl.h` 或 `lv_version.h` 中 `LVGL_VERSION_*` 宏。
* CherryUSB：`usb_version.h` 中 `CHERRYUSB_VERSION_STR`。
* OpenAMP / libmetal / SpeexDSP / FLAC / zlib / libpng / libjpeg-turbo / wpa_supplicant：`VERSION` / `configure.ac` / `*.h` / `ChangeLog.md` 中明确写出。
* libpeer / Zephyr HTTP / cJSON（bouffalo_sdk）：本仓库专门的 `version` 文件列出了 upstream git commit。
* GmSSL / TF-A：目录名内嵌版本号。

「猜出来的」/ 推断的：
* bouffalo_sdk 的 `wifi4/wpa_supplicant` 与 `bl_wpa_supplicant`：里头没有 `version.h`，按上游目录结构推断属 hostap 2.x 系列；只 wifi6 那一份有显式 `VERSION_STR "2.10"`。
* OpenThread：通过 `OPENTHREAD_API_VERSION 512` 反推大约对应 2024 年上游，未给出 SemVer。
* `Bouffalo blcrypto_suite`：`version` 文件指向 `mbedtls v2.16.11` 上游 tarball，但内部源码已被改名（`blcrypto_suite_*.c`），实际所用 mbedtls 子集来自 2.16.11。
* MQTT-C / Tremolo / Helix AAC / libgsm / libopus / iperf3 / dnrd / libwsclient / KissFFT / tflite-micro：上游未把版本写进文件，需要 `git log` 才能精确，本表标 `?`。
* `ameba-rtos-d` 下 `mDNS`、`httpc`、`httpd`、`rtsp`、`websocket` 等：源码极少（多数实现在 Realtek 闭源 `.a`），上游归属不可靠地确定，故不单列。
* `dnrd` 在本 SDK 中只剩 `lib_ipdnrd.a` 一个静态库，未保留源码；license 按上游 dnrd 项目（DNS Relay Daemon，GPL-2.0-only）记录，需要 Realtek 二次确认是否携带源码或选用了不同的 fork。

License 没找到 / 不确定：
* bouffalo_sdk 的 `multimedia/` 下 11 个子目录都没有独立 LICENSE 文件，按目录名映射到上游项目（libopus / libvorbis / speex / Helix AAC 等）推断 license。
* `Bouffalo blestack/src/sbc` 用 Bluedroid / Open Interface SBC，文件头声明 Apache-2.0，但目录里没有专门的 LICENSE 文件。

