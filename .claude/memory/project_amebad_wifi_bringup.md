---
name: amebad-wifi-bringup
description: AmebaD KM4 WiFi bring-up status — scan-milestone firmware built & links; what's wired, the scan-only stub strategy, what remains, HW verify pending
metadata:
  type: project
  originSessionId: 2c7314f2-7f7e-46e5-84f0-75d717be0da0
---

目标:driver-only AmebaD 固件在 KM4 把 host WiFi 驱动 init 起来,做到
`wifi_on(RTW_MODE_STA)` + `wifi_scan` 列出 AP(验收线=scan,连接/DHCP/AT 后续)。
KM0 侧 WiFi FW 加载链路本就全启用(见 [[amebad-sdk-decoupling-status]] 不涉及,
反汇编确认 `km0_config_wifi_enable=TRUE`→`BKUP_Set(0,BIT_WIFI_ENABLE)`→
`wifi_FW_init_ram()`)。

**状态(2026-06-04):scan 里程碑 ✅ 实机验证通过(COM29 单口烧录+sniff)**。
启动日志:`[KM4] wifi_on OK (heap 0x45a80)` → `[KM4][scan] 9c:9d:7e:..  ch 40
-57 dBm  AIoT` ...44 个 AP(2.4G+5G 真实 SSID)→ `==== scan complete ====`,
无崩溃。**KM4 host 驱动 + KM0 WiFi FW + 双核 IPC 握手整条链路打通**。
`Image_All.bin` 从 135168 → **507904 bytes**;KM4 image2 483328 bytes
(ram bss ~35K + nocache ~24K,BD_RAM_NS 476K 内;XIP text ~355K 进 flash)。

**之前担心的 IPC ch1 握手风险没出现** —— `usrcfg/rtl8721d_ipccfg.c` KM4 侧 ch1
保持 NULL handler 也没事,wifi_on 没卡,说明 WiFi 驱动运行时自己注册了 IPC 收端,
不需要静态表里挂。没改 ipccfg。

**踩坑(已修,commit 65bab05)**:scan 回调别用 DiagPrintf 的空格补齐宽度符
(`%2u`/`%4d`)——Realtek ROM printf 只认 `%0Nx`,喂这俩解析错位→后面 `%s` 拿错位
实参当指针解引用(0x48012000 外设)→ secure hard fault。只能用纯 `%d`/`%x`/`%s`;
SSID 先安全拷本地 null-terminated 再打。烧录:单口 `flash.ps1 -BootPort COM29
-FlashPort COM29 -Image <UNC>`(image 用 `//wsl.localhost/Ubuntu-24.04/...` 正斜杠
让 Resolve-WslPath 转;ImageTool 从 \\wsl.localhost UNC 跑 OK)。

**组件结构(component/amebad/,全 KM4-gated)**:
- `wlan/` = lib_wlan*.a(4 变体)/lib_wps.a(IMPORTED)+ **驱动源码胶水**
  `amebad_wlan_glue`(src/lwip_intf.c + core/option 6 个:skb池/RF参数/ram map/
  power)+ 驱动 osdep 头(osdep/:skbuff/wlan_intf/lwip_intf/wireless/bt_intf/
  wrapper)+ rom/rom_wlan_ram_map.h。`rtw_opt_crypto_ssl.c` vendored 但**不编**
  (用 mbedtls 2.4.0 私有化的 ecp_point.X/Y/Z,连接/EAP-TLS 路径)。
- `lwip/` = lwipcore + amebad_lwip(sys_arch + **ethernetif**)。Realtek port 用扁平
  lwip include(err.h),故把 `src/include/lwip` 也加进搜索路径。
- `wifi/`(新)= api/wifi 的 wifi_conf/util/ind.c(wifi_on/wifi_scan)+ api/network
  的 wlan_network.c/lwip_netconf.c(LwIP_Init)+ 头,全拍平一层;`amebad_wifi`。
  rooted include `<wifi/..>`/`<at_cmd/..>` 经 `component/amebad` 根解析;
  `<dhcp/..>`/`<wlan_fast_connect/..>`/spdio/flash_api 经 sdk_headers 镜像根。
- `eap/`(lib_eap.a)、`wpa_supplicant/`(配置源+头)已在但 scan 不链。

**app_km4 接线**:main() 起 `wifi_bringup_thread` → LwIP_Init → wifi_on(STA) →
wifi_scan_networks(回调打印 SSID/RSSI/ch)。link group 加 amebad_wifi/wlan_glue/
lwip/lwipcore/wlan/wps。

**scan-only 收链接闭包的手法(关键,连接时要回填)**:
- `wifi_scan_compat.c`(app_km4):`wifi_mode`(wifi_conf.c 仅 CONFIG_MBED_ENABLED
  下定义,本配置关)给 weak 真实全局;`get_eap_phase/method`、
  `eap_autoreconnect_hdl`、`promisc_deinit/recv_func`、
  `mbedtls_platform_set_calloc_free` 给 weak 桩(连接/EAP/promisc/TLS,scan 不触发)。
  全 weak → 将来编真实 wifi_eap_config.c/wifi_promisc.c/mbedcrypto 自动覆盖。
- `PAD_CMD`:lib_wlan.a 要的 _LONG_CALL_ ROM pinmux,app_km4 ROM 符号表没列;
  `rom_symbol_wifi.ld` 加 `PROVIDE(PAD_CMD=0x10102f99)`(取自 bootloader_km4 ROM 表)。
- `-fcommon`:Realtek 旧代码头里声明无 extern 全局(uart_buf 等),GCC10 默认
  -fno-common 报 multiple definition;给 amebad_wifi/amebad_wlan_glue 加回 -fcommon。

**剩余**:① 实机烧录验 scan(板接好后,两口 `project/amebad/flash.ps1
-Image build/amebad/Image_All.bin` 进 download mode 烧,再 `sniff_log.ps1
-Baud 1500000` 看 `[KM4] wifi_on OK` + `[KM4][scan]` AP 列表);**最大风险=KM4↔KM0
  IPC ch1 握手**(KM0 FW 已启用,但 KM4 ipc_init_config ch1 之前是 NULL handler,
  wifi_on 卡住多半在这,排查 `driver_fw_flow_ipc_int` 双向)。② 之后才是 connect+
  DHCP+ping、AT 命令、把上面 weak 桩换真实现。

提交:b4fb13d(库层)+ 2dccb48(app_km4 接线)。计划文件
`.claude/plans/component-amebad-clever-owl.md`(已批准,6 阶段,1-5 完成,6 待实机)。
