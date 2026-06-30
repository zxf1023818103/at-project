---
name: project_amebad_ble_dtm
description: "driver-only KM4 镜像里移植 BLE DTM(bt_power,on/gnt_bt/bridge)的组件结构、链接坑、HCI 验证状态"
metadata: 
  node_type: memory
  type: project
  originSessionId: 2c7314f2-7f7e-46e5-84f0-75d717be0da0
---

把原版 SDK 的 BLE DTM(`ATM2=bt_power,on`/`gnt_bt,bt`/`bridge`)移植成 in-tree 组件
`component/amebad/hci`,build flag `-DAMEBAD_ENABLE_HCI=ON` 出专用测试镜像(默认 OFF,
不影响 WiFi 镜像)。文档 `doc/amebad_ble_dtm.md`。

**关键事实**:
- prebuilt `btgap.a`(629KB,`bte_init`=T)是整个 BLE 栈;63 个 undefined 全可解:49 osif_*
  (osif_freertos.c)+ 6 hci_tp_*(hci_adapter.c)+ 2 ftl_*(in-tree amebad_ftl)+ coex/secure
  全在 in-tree lib_wlan.a。**不用 vendor rtk_coex.c/vendor_cmd.c**。
- **组件全平铺**(house style,跟 ftl/osdep/monitor 一样 subdirs:0):vendored 源/头 + btgap.a
  直接在 `component/amebad/hci/`,无 `sdk/` 子树。可行性:头无 basename 冲突 + 全树零 subpath
  `#include` → 一个 flat include 根解析全部。丢了 rtl8721d_bt.c(要 drv_types.h)。
- **头文件已剪**(用户为后期换 NimBLE 清理):按编译源 `#include` 传递闭包,64 头里只 28 个被引用,
  删了 36 个未引用的原厂上层栈 API 头(GATT 服务 bas/dis/hids/ancs、gatt.h、profile_*、
  simple_ble_*、gcs/gaps_client;GAP 应用层 gap_adv/bond/scan/privacy/config;bte.h、aes/crc、
  BT 专用 ftl_*、高层 os_msg/queue/task/timer.h)。删后 normal+mp 强制重编全过。换 NimBLE 时这些
  连 btgap.a 一起撤。闭包算法:正则抓所有 #include(含 #if-guarded,保守保留)从 .c 起传递求闭包。
- **⚑ btgap.a 已彻底删除(现状,2026-06)**:原是 `AMEBAD_BT_NO_BTGAP` opt-in PoC,验证通过后
  转正为唯一路径并删除。删除清单:`btgap.a`、coex 源 `rtk_coex.c`/`vendor_cmd.c` + 其头、只被 coex 用
  的 7 个 `gap_*.h`;`amebad_btgap` target、`AMEBAD_BT_NO_BTGAP` option 一并去掉。组件收敛到 18 源
  +18 头(头闭包从 28→18),镜像 618496 B(曾含 btgap 712704)。
  - **为什么能删**:HCI 层对 btgap 全部依赖=9 符号(bte_init + 4 coex gap_*/le_get_* + 4 工具
    os_delay/os_mem_free/os_mem_zalloc_intern/trace_log_buffer);hci_uart.c 对 btgap 0 依赖。
  - **bte_init → `hci_nostack.c::bt_hci_bringup_nostack()`** 直接调 `hci_tp_open()`(本就是我们的代码,
    跑完整 bring-up;bte_init 原来只是替我们调它+起 host,我们不要 host)。4 个工具符号在 hci_nostack.c
    用 osif/FreeRTOS 重实现。
  - **关键坑**:rx_ind 在 UART RX 中断里直接跑 hci_tp_config(patch 下载重活)→ 中断栈溢出 →
    HardFault CFSR=0x400 INVSTATE(卡在 Set baudrate 后);修法=rx_ind 空操作(中断只填 ring),事件
    重组+hci_tp_config 放任务上下文(wifi_bringup_thread 16KB 栈)轮询跑。
  - 实测:`BT INIT success` + `HCI bring-up OK (no btgap.a)` + 5 条裸 HCI 全回应。normal+mp 都重编过。
  - ⚠ 下面"normal 用 bt_coex_init""链接三条铁律①start-group""bte_init"等是删 btgap **之前**的历史,
    已不适用(coex/bt_coex_init 没了;LINK_GROUP 现只 `,amebad_hci`;bring-up 走 hci_tp_open 不是 bte_init)。
- **normal/mp 固件切换 = `option(AMEBAD_HCI_FW_MP OFF)`**:ON→编 hci_mp_patch.c + 定义
  CONFIG_MP_INCLUDED(hci_find_patch_address 选 rltk_bt_get_mp_patch、bt_dtm 打印 MP banner、
  RF 用裸 gnt_bt);OFF→hci_normal_patch.c + **normal 用 bt_coex_init**。bring-up(bte_init)两者
  相同,只差下载哪个 ~12KB patch + RF 处理。normal+mp **都实机验证过**(BT INIT success 7)。
- **RF/天线按固件分**(用户要求"正常模式切换天线函数改成 bt_coex_init"):MP=
  `rtlk_bt_set_gnt_bt_with_clk_source(PTA_BT)`(bte_init 前,裸 grant 全判 BT);normal=
  `bt_coex_init()`(bte_init **后**,= vendor_cmd_init→gap_register_vendor_cb 的 PTA WiFi/BT 共存;
  放 bte_init 后因为注册 GAP vendor 回调要栈先加载,对齐 SDK ble_app_main)。为此补 vendor 了
  rtk_coex.c + vendor_cmd.c(原 plan 没 vendor;bt_coex_init 只是 vendor_cmd_init(NULL))。实机
  `bt_coex_init done` + HCI 仍往返✅。
- **链接三条铁律**:
  ① btgap.a↔amebad_hci 互引 → 必须同在 `--start-group`(LINK_GROUP cross_refs 追加 `,amebad_hci,amebad_btgap`)。
  ② newlib syscall stub(hci_syscalls.c)进 **executable** 而非 lib(libc 右侧引用,lib member 不预引用不拉)。
  ③ **固件 blob(bt_normal/mp_patch.c)也必须进 executable,不能放 amebad_hci archive**:
     hci_fw_stub.c 的弱 rtlbt_(mp_)fw[]={0} 会满足 rltk_bt_get_*_patch() 引用 → linker 不再拉
     定义强 blob 的 .o → 控制器拿空 patch,实机报 `we use single patch / not support single
     patch on rom / error operate`。放 executable 则强符号必链 override 弱桩。nm 核:mp 镜像
     rtlbt_mp_fw=T 0x2fe0(真)、rtlbt_fw=V 1B(弱桩)。hci_fw_stub.c 现给全弱兜底(俩 body+俩 *_fw_len)。
  `--gc-sections` 丢未用 BLE 上层栈 → image2 +98KB(688128 B,远在 OTA1 ~1.53MB 内)。
- hci_compat.c:vendored `UART_RxMonitorSatusGet`(RAM-only fwlib 一行 return UARTx->MON_BAUD_STS)
  + `printf` wrapper(_rtl_vsnprintf+DiagPrintf,不拉 newlib stdio)。
- hci_board.c patch:`hci_find_patch_address()` 强制走 in-image patch(rltk_bt_get_normal_patch),
  避开 ota_get_cur_index。

**HCI 验证状态(实机 ✅)**:`bt_dtm_bringup()` = gnt_bt(PTA_BT)+bt_trace_init+bte_init。bte_init
全程就是真实 HCI 来回(读版本→读ROM→设波特921600→下载71KB patch→HCI Reset→读BD_ADDR)。
控制台见 `bte_init OK`、rom_version 0x0002、chip_id 0x0003、BT ADDRESS 94:c9:60:b7:0e:db、
`BT INIT success 7`、`Start upperStack`。**这满足"测试 HCI 命令"**。

**host 驱动 bridge 也实机往返通过 ✅**(bt_dtm_enter_bridge + project/amebad/hci_test.ps1):五条裸
HCI(Reset/Read Local Version/Read BD_ADDR/LE Transmitter Test/LE Test End)全拿合法 Command
Complete。**关键坑**:SDK 的 bt_uart_bridge_open() 依赖两个中断,driver-only KM4 上都不送数据
——host→ctrl 那条 LOGUART RX 中断被 shell(占 UART_LOG_IRQ)静默吞掉非 `\r` 字节;ctrl→host 那条
transport IRQ+set_hci_uart_out 的 bridge_flag 也没到 hci_uart_rx_bridge。**修法=放弃 SDK bridge,
全双向轮询**:InterruptDis(UART_LOG_IRQ) 抢 host UART2 RX、UART_INTConfig(UART1,ERBI|ETOI,DISABLE)
抢 ctrl UART1 RX,紧循环直接 UART2↔UART1 倒字节(UART_Readable/CharGet/CharPut)。HCI_UART_DEV=
UART1_DEV、host=UART2_DEV(LOGUART,hal_platform.h 注释"KM0 log uart")。诊断法:P%02x(poll 到的
host 字节)/R%02x(到 bridge 的回应)逐字节打印定位是哪个方向断。hci_test.ps1 sync 在 "entering HCI
bridge" marker(boot 慢~30s,固定 delay 不可靠)。main.c 现默认进 bridge。
对照 [[reference_amebad_hw_crypto]] 同类"driver-only 调 secure/独立 silicon"。
- **coex 算法在 btgap.a 外面**(实测:btgap.a 零 coex 符号):①算法本体在 lib_wlan.a(hal_coex_run_case/
  rtl8721d_btcoex_run_case 决策引擎、rtl8721d_pstdma_table/set_pstdma_cmd/set_coex_table TDMA 时隙表、
  hal_coex_set_external/internal_pta、coex_hal_fun);②BT 侧 PTA 在控制器固件(rtlbt_fw,"BTCOEX Version"
  串);③host 胶水=我们的 rtk_coex.c/vendor_cmd.c(BT 状态→rltk_coex_mailbox_to_wifi)。btgap.a 只贡献 GAP
  管道(gap_register_vendor_cb/gap_vendor_cmd_req/le_get_conn_param/le_get_gap_param),非算法。**换 NimBLE 时
  coex 算法原封不动**(lib_wlan.a+控制器固件),只需 NimBLE 提供等价"发 vendor HCI+取连接参数"+把 BT 状态接回
  rltk_coex_mailbox_to_wifi。**现状:第③层 host 胶水(rtk_coex.c/vendor_cmd.c)已随 btgap.a 删除**——当前
  镜像不喂 BT 状态给 WiFi 算法(纯 DTM/HCI 无影响,bt_dtm_bringup 仍 gnt_bt 把 RF 判给 BT);①②层还在。
  coex 架构已记入 doc/amebad_ble_dtm.md「WiFi/BT coex」节。
- **控制器是纯 BLE(不支持经典蓝牙 BR/EDR)**:实测发经典专属命令(Read Class of Device 0x0C23 /
  Read Scan Enable 0x0C19 / Read Voice Setting 0x0C25 / Read Page Timeout 0x0C17)全返回 status
  0x01 "Unknown HCI Command";控制器名 "RTK_BT_5.0";Read Buffer Size 的 SCO 缓冲=0。**坑:特性位会
  骗人**——Read Local Supported Features(0x1003)/Supported Commands(0x1002)位图报了 BR/EDR 位
  (silicon ROM 默认表),但加载的 BLE 固件运行时拒掉,**得发真命令看 status 才算数**。版本:HCI/LMP
  ver 0x09=BT5.0、厂商 0x005D Realtek;LE features 有 2M PHY+Coded PHY+DLE(251B)。查询脚本
  project/amebad/hci_info.ps1。→ 换 NimBLE(LE-only host)正好对得上。
- **不打 patch 也不支持经典蓝牙**(`option(AMEBAD_HCI_SKIP_PATCH OFF)`,裸 ROM 探针):跳过 patch 下载
  (hci_process.c 表里去掉 UPDATE_BAUDRATE+DOWNLOAD_PATCH 两步 + hci_read_rom_check 里跳过
  hci_rtk_find_patch;控制器停在 115200 boot baud,"BT INIT success 4" vs 打 patch 的 7)。裸 ROM 的
  BR/EDR 命令探测结果与打 patch **逐字节相同**(Class of Device/Scan Enable/Voice Setting/Page Timeout
  全 0x01)→ 经典蓝牙是 silicon ROM 层面就没有,不是 patch 阉割的。**附带**:裸 ROM LMP subver=0x8721,
  打 patch(no-btgap 路径)=0x61EB(normal patch 文件名号)→ 证明 patch 真生效改了版本号。
