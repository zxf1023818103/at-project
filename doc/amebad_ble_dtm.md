# AmebaD (RTL8721D) — BLE DTM 组件移植 + HCI 命令验证

把原版 SDK 的 BLE DTM(Direct Test Mode)bring-up 路径移植成 driver-only KM4 镜像里
的一个 in-tree 组件 `component/amebad/hci`,并验证 HCI 命令真实来回。结论先行:
**组件已移植并随 KM4 image2 链接通过;BT 控制器在实机上经 HCI-UART 完整 bring-up
(读版本 → 读 ROM → 设波特 → 下载 71 KB 固件 patch → HCI Reset → 读 BD_ADDR),
证明 HCI 命令/事件来回工作。** host 驱动的透明 bridge(`ATM2=bridge`)已实现、能开,
但裸 HCI 透传的往返还需用户的真实 BT 测试工具验证(见末节)。

## 原版 SDK 的三条命令

原版固件用三条 `ATM2=` 测试命令开 BLE DTM(`atcmd_mp_ext2.c`):

| 命令 | 实质动作 |
|---|---|
| `ATM2=bt_power,on` | `rtlk_bt_set_gnt_bt_with_clk_source(PTA_BT)`(把天线/RF grant 给 BT)+ `bt_trace_init()` + `bte_init()`(把控制器拉起来:经 HCI-UART 下载固件 patch + 跑 HCI 初始化序列) |
| `ATM2=gnt_bt,bt` | WiFi/BT 共存把 RF 判给 BT(同一个 coex grant) |
| `ATM2=bridge` | `bt_uart_bridge_set()` + `bt_uart_bridge_open()` + `set_hci_uart_out(true)`:劫持 LOGUART(UART2)当透明 HCI 管道,host 直接对控制器的 UART1 发裸 HCI |

本任务把 **控制器 bring-up + HCI 传输 + UART bridge** 这套移植进 driver-only image。
用户选型:**normal 固件**(标准 BLE DTM,非 MP/RF-test)+ **开机自动进入 DTM**
(build flag 专用测试镜像,不影响默认 WiFi 镜像)。

## 可行性(已 `nm` 核实)

driver-only image 本就没有完整的 BT/AT 栈,关键是确认 prebuilt 的 BLE 栈
`btgap.a` 的外部符号都能在已 in-tree 的东西里解析:

- **整个 BLE 栈(gap/gatt/l2c/sm/att/hci_if/bte)都在 prebuilt `btgap.a`(629 KB)里**
  (`bte_init` = T)。对外 63 个 undefined,全可解:
  - 49 个 `osif_*` → 移植 `osif_freertos.c`
  - 6 个 `hci_tp_*` → 移植 `hci_adapter.c`
  - 2 个 `ftl_*` → **已 in-tree** `amebad_ftl`
  - `osif_create_secure_context` → `rtw_create_secure_context`(in-tree `lib_wlan.a`)
  - `platform_random` / `trace_print` → 移植源
  - `memcmp/cpy/set/strlen` → newlib
- **共存 grant 符号全在我们 in-tree 的 `lib_wlan.a`(T)**:
  `rtlk_bt_set_gnt_bt_with_clk_source`、`rltk_coex_bt_enable`、`rltk_coex_mailbox_to_wifi`、
  `rltk_bt_get_patch_code`、`bt_get_patch_code_8721d`、`rtw_create_secure_context`。
  KM4 已链 `amebad_wlan` → 共存白拿,**不用 vendor `rtk_coex.c` / `vendor_cmd.c`**。
- BT 控制器是**独立 silicon**,固件经 HCI-UART(8721D 用 UART1)下载;`hci_board_init`
  只要 WiFi **on**(非 connected),我们 KM4 `wifi_bringup_thread` 已 `wifi_on(STA)`。
  **不是 secure-gated 外设**(区别于之前调不通的硬件 crypto 引擎),走 HCI/UART,
  可行性同 WiFi bring-up。

## 组件结构 `component/amebad/hci`(KM4-gated)

**布局:全拍扁(house style)**——所有 vendored 源/头直接平铺在 `component/amebad/hci/`,
没有 `sdk/` 子树。可行性已核:头**无 basename 冲突**、整棵 BT 树**零 subpath `#include`**
(全裸文件名),所以一个 flat include 根(本目录)就解析所有 `#include`。源文件全来自
`platform/ameba-rtos-d/component/common/bluetooth/realtek/sdk/`。

**`btgap.a`(原厂上层栈)已移除** —— 见末节"移除 btgap.a"。控制器 bring-up 完全在我们的
HCI 传输里,不需要原厂 BLE 栈;后续上层换 NimBLE。

**头文件已剪到 18 个**:原样复制有 64 头。先按"编译源 `#include` 传递闭包"删了 36 个未引用的
BLE 应用/profile API 头(GATT 服务 `bas/dis/hids/ancs`、`gatt.h`、`profile_*`、`simple_ble_*`、
`gcs/gaps_client`;GAP 应用层 `gap_adv/bond/scan/privacy/config`;`bte.h`、aes/crc、BT 专用
`ftl_*`、高层 `os_msg/queue/task/timer.h`);移除 btgap.a + coex 后又删了 10 个只被 coex 用的
(`gap.h`/`gap_le*`/`gap_msg`/`gap_conn_le`/`gap_callback_le`/`gap_storage_le`、`rtk_coex.h`、
`vendor_cmd.h`/`vendor_cmd_bt.h`)。剩 18 头全是 HCI 传输 + osif + trace。删后 normal+mp 强制
重编全过(证明确实没引用)。

**编进 `amebad_hci`(STATIC)的 18 个 .c**(全部平铺在 `hci/`):

| 文件 | 作用 |
|---|---|
| `osif_freertos.c` | 49 个 `osif_*`(OS 抽象,原供 btgap.a 调,现由 hci_nostack 调) |
| `hci_adapter.c` | `hci_tp_open/close/send/recv`(`hci_tp_open` = 完整 bring-up) |
| `hci_process.c` | `HCI_PROCESS_TABLE`(patch 下载 + reset 序列) |
| `trace_task.c` | `bt_trace_init` / `trace_print` |
| `cycle_queue.c` | 环形队列 |
| `hci_uart_bridge.c` | SDK LOGUART relay(被 hci_dtm 轮询 bridge 取代;仍提供 `bt_uart_tx`,被 hci_uart.c 引用) |
| `hci_board.c` | `bt_reset`/`bt_power_on`、`hci_board_init`、patch 选择 |
| `hci_uart.c` | UART1 HCI 驱动、`set_hci_uart_out` |
| `platform_utils.c` | `platform_random` |
| `trace_uart.c` | trace UART |
| `hci_fwconfig.c` | `rtlbt_config` + `rltk_bt_get_normal/mp_patch` |
| `hci_nostack.c`(in-tree) | **替 bte_init**:直接 `hci_tp_open()` 起控制器 + 重新实现 btgap.a 原来的 4 个工具符号(`os_delay`/`os_mem_free`/`os_mem_zalloc_intern`/`trace_log_buffer`) |
| `hci_dtm.c`(in-tree) | bring-up glue + 轮询 HCI bridge(见下) |
| `hci_fw_stub.c`(in-tree) | weak `rtlbt_fw`/`rtlbt_mp_fw`/`*_fw_len`(替 `rtl8721d_bt.c`,避开 `drv_types.h` 整棵 WLAN 头) |
| `hci_compat.c`(in-tree) | `UART_RxMonitorSatusGet`(RAM-only fwlib 一行)+ `printf` → ROM `_rtl_vsnprintf`+`DiagPrintf`(不拉 newlib stdio) |

(另 `hci_normal_patch.c`/`hci_mp_patch.c` 固件 blob + `hci_syscalls.c` 进 app_km4 **executable**,见下。)

**固件 blob 不在 `amebad_hci` 里,而在 app_km4 executable**(`hci/hci_normal_patch.c` /
`hci/hci_mp_patch.c`,~12 KB 二进制 patch)。原因见下文"`AMEBAD_HCI_FW_MP` 选项"——放进
archive 的强 blob **不会被拉**(弱桩已满足 `rltk_bt_get_*_patch()` 引用),控制器会拿到空
patch。放 executable 则强符号必链、override 弱桩。

**bring-up glue `hci_dtm.c`**:
- `bt_dtm_bringup()` = `rtlk_bt_set_gnt_bt_with_clk_source(PTA_BT)`(把 RF 判给 BT)+
  `bt_trace_init()` + `bt_hci_bringup_nostack()`(hci_nostack.c → `hci_tp_open()`:下 patch + HCI
  reset)。MP/normal 只差下载哪个 patch blob。
- `bt_dtm_enter_bridge(host_baud, parity)` = 全双向轮询 HCI bridge(见末节)。

**链接接线**:`app_km4/CMakeLists.txt` 的 `$<LINK_GROUP:cross_refs,...>` 追加 `,amebad_hci`
(btgap.a 已移除,不再有 `amebad_btgap`)。

## 固件选型:`AMEBAD_HCI_FW_MP` 编译 option

normal(标准 BLE DTM)vs mp(RF 厂测)只差**控制器下载哪个固件 patch**——bring-up 路径
(`bte_init`)完全相同(SDK 的 `mp_ext2_bt_power` 也是 `gnt_bt(PTA_BT)`+`bte_init`,没有
单独的 mp 入口)。

```bash
cmake build/km4 -DAMEBAD_ENABLE_HCI=ON -DAMEBAD_HCI_FW_MP=ON   # mp
cmake build/km4 -DAMEBAD_ENABLE_HCI=ON -DAMEBAD_HCI_FW_MP=OFF  # normal(默认)
```

`AMEBAD_HCI_FW_MP=ON` 做两件事:① 把 `hci_mp_patch.c`(强 `rtlbt_mp_fw`)而非
`hci_normal_patch.c` 编进 executable;② 给 `amebad_hci` 定义 `CONFIG_MP_INCLUDED`,使
`hci_find_patch_address()` 返回 `rltk_bt_get_mp_patch()`(而非 normal),`hci_dtm.c` 打印
`(MP / RF-test fw)` banner。

**archive-pull 陷阱(关键)**:固件 blob 起初编在 `amebad_hci` archive 里,mp 镜像跑出
`we use single patch / not support single patch on rom / error operate`——因为
`hci_fw_stub.c` 的**弱** `rtlbt_mp_fw[]={0}` 已满足 `rltk_bt_get_mp_patch()` 的引用,
linker **不再去拉**定义强 `rtlbt_mp_fw` 的 `bt_mp_patch.o`(static archive member 只有在有
**未决**引用时才被拉),控制器拿到空 patch、`tmp_patch_head` 不是 "Realtech"。
**修法**:把当前生效的 blob `.c` 放进 **executable**(executable object 无条件链接,强符号
必 override 弱桩),archive 里只留 `hci_fw_stub.c` 的全弱兜底(两个 blob body + 两个 `*_fw_len`)。
nm 核实:mp 镜像里 `rtlbt_mp_fw` = `T` 0x2fe0(真 blob)、`rtlbt_fw` = `V` 1B(弱桩)。

## 移植踩坑(whack-a-mole)

| 症状 | 修法 |
|---|---|
| `osif_freertos.c:412` "expected declaration specifiers" | 本地 `extern BaseType_t xTaskNotifyStateClear(...)` 与 FreeRTOS V10.6.2 的宏冲突 → 删本地 extern |
| `hci_board.c:21` 缺 `build_info.h` | 塞 2 行 shim(只用到 `UTS_VERSION`) |
| `rtl8721d_bt.c` 要 `drv_types.h`(整棵 WLAN 头树) | 丢 `rtl8721d_bt.c`(`bt_get_patch_code_8721d` 在 `lib_wlan.a`),用 `hci_fw_stub.c` 给 weak `rtlbt_fw`/`rtlbt_mp_fw` |
| `hci_dtm.c` "conflicting types for DiagPrintf" + "'true' undeclared" | 删自己的 `extern DiagPrintf`(头里声明 u32),`true` 改 `1` |
| `-Dprintf=DiagPrintf` 把 `diag.h` 的 printf token 改坏 | 改成 `hci_compat.c` 里真正的 `printf` wrapper(`_rtl_vsnprintf`+`DiagPrintf`) |
| `ota_get_cur_index` undefined | patch `hci_board.c` `hci_find_patch_address()` 强制走 in-image patch(`CONFIG_MP_INCLUDED` 选 `rltk_bt_get_mp_patch` / `rltk_bt_get_normal_patch`) |
| `UART_RxMonitorSatusGet` undefined(RAM-only fwlib,KM4 ram_common 子集排除) | `hci_compat.c` 里 vendored 一行(`return UARTx->MON_BAUD_STS`) |
| newlib syscall(`_write`/`_sbrk`/`_exit`/...)undefined | `hci_syscalls.c` retarget;**关键**:必须放进 **executable**(`target_img2_km4`)而非 `amebad_hci` lib——libc 在链序右侧引用它们,lib member 不被预引用就不拉,executable object 总是链 |

## HCI 命令验证(实机)

`-DAMEBAD_ENABLE_HCI=ON` 出专用镜像,`build_image_all.py` 全量构建,
`flash.ps1` 烧 COM29,`sniff_log.ps1` 抓 boot log。**`bte_init()` 内部跑的就是真实
HCI 命令/事件来回**(`hci_tp_open` → `hci_board_init` → `bt_reset`/`bt_power_on` →
`HCI_PROCESS_TABLE`):读版本 → 读 ROM → 设波特 → 下载 ~12 KB patch → HCI Reset →
读 BD_ADDR。**normal 与 mp 两个固件都实机验证通过**,控制台输出(mp 镜像):

```
[BT] DTM bring-up (MP / RF-test fw): gnt_bt(BT) + bt_trace_init + bte_init ...
BT BUILD Date: at-project-bt-dtm
BT Reset...
[BT] bte_init OK — controller up, patch downloaded, HCI reset done
physical efuse: has data hci_tp_phy_efuse[1]= 9e
bt_iqk_dump:    DUMP, ...
hci_read_rom_check: rom_version 0x0002, bt_hci_chip_id 0x0003
BT ADDRESS: 94:c9:60:b7:0e:db
Set baudrate to 921600
hci_tp_config:BT INIT success 7
Start upperStack
```

(normal 镜像 banner 是 `(normal BLE fw)`,其余一致。)读到 `rom_version 0x0002` /
`chip_id 0x0003` / `BT ADDRESS 94:c9:60:b7:0e:db` 都是控制器对 HCI 命令返回的事件数据
—— **HCI 命令链路验证通过**,无 hardfault / hang。早先 mp 镜像出过
`we use single patch / not support single patch on rom`,是固件 blob 没被链进来(空 patch),
已由"固件选型"一节的 archive-pull 修法解决。

## footprint

| | bytes |
|---|---|
| KM4 image2(BT 镜像 normal,`AMEBAD_HCI_FW_MP=OFF`,**无 btgap.a**) | 618496 |
| KM4 image2(BT 镜像 mp,`AMEBAD_HCI_FW_MP=ON`,**无 btgap.a**) | 618496 |
| (参考)曾含 btgap.a 时 | 712704 |
| OTA1 区预算 | ~1.53 MB |

`--gc-sections` 丢掉未用 BLE 上层栈后,BT 镜像比基线 WiFi image 只大 ~98 KB,远在
OTA1 预算内。(注:当前 BT 测试镜像仍带着基线的 KAT / wolfCrypt bench / iperf,boot
多 ~6 s;真正精简的 DTM-only 镜像可把这些关掉,但不影响 HCI 验证。)

## host 驱动 bridge(`ATM2=bridge`)— 实机往返通过 ✅

`bt_dtm_enter_bridge()` 把 LOGUART(COM29)变透明 HCI 管道,host 用
`project/amebad/hci_test.ps1`(`System.IO.Ports` 裸 HCI 写 byte[] + 读 hex dump,sync 在
`entering HCI bridge` marker 上)实测**五条 HCI 命令全部拿到合法 Command Complete**:

| host 发 | 控制器回 | 解码 |
|---|---|---|
| HCI Reset `01 03 0C 00` | `04 0E 04 03 03 0C 00` | opcode 0x0C03,status 00 |
| Read Local Version `01 01 10 00` | `04 0E 0C 02 01 10 00 09 0D 00 09 5D 00 21 87` | HCI ver 0x09(BT5.0),厂商 0x005D(Realtek),LMP subver 0x8721 |
| Read BD_ADDR `01 09 10 00` | `04 0E 0A 02 09 10 00 87 99 23 4C E0 00` | BD_ADDR 00:E0:4C:23:99:87(MP fw 默认地址) |
| LE Transmitter Test `01 1E 20 03 00 25 00` | `04 0E 04 02 1E 20 00` | opcode 0x201E,status 00(ch0 开始发射) |
| LE Test End `01 1F 20 00` | `04 0E 06 02 1F 20 00 00 00` | opcode 0x201F,status 00,包数 0 |

**实现要点(踩坑)**:SDK 的 `bt_uart_bridge_open()` 依赖**两个中断**,在我们 driver-only
KM4 镜像里都不送数据:
- host→controller:SDK relay 靠 LOGUART RX 中断(注册在 `UART_LOG_IRQ_LP`),但本镜像
  shell 占着 `UART_LOG_IRQ` 把非 `\r` 的 HCI 字节静默缓冲,relay handler 从不触发(实测 0
  字节到 bridge);
- controller→host:走 transport IRQ + `set_hci_uart_out()` 的 `bridge_flag` 路由也没到
  `hci_uart_rx_bridge`(实测 0 回应字节)。

所以 `bt_dtm_enter_bridge()` **不用 SDK bridge,改全双向轮询**:`InterruptDis(UART_LOG_IRQ)`
把 host LOGUART(UART2)RX 从 shell 抢走、`UART_INTConfig(UART1, ERBI|ETOI, DISABLE)` 把
controller HCI UART(UART1)RX 从 BT transport 抢走,然后紧循环里直接在 UART2↔UART1 之间
倒字节(`UART_Readable`/`UART_CharGet`/`UART_CharPut`)。绕开所有 IRQ-ownership/`bridge_flag`
假设,确定性工作。诊断时用过 `P%02x`(poll 到的 host 字节)/`R%02x`(到 bridge 的回应字节)
逐字节打印定位方向,定位后撤掉。

**结论**:控制器 bring-up 已证 HCI 工作,host 驱动 bridge 也实测裸 HCI 往返通过——
`ATM2=bt_power,on/gnt_bt/bridge` 三条命令对应的能力全部移植 + 实机验证。

## 移除 btgap.a(原厂上层栈)✅

btgap.a 及其 host 胶水已**从组件里删除**(为换 NimBLE 铺路;原是 `AMEBAD_BT_NO_BTGAP` opt-in 实验,
验证通过后转正为唯一路径)。删除清单:`btgap.a`、coex 源 `rtk_coex.c`/`vendor_cmd.c`、它们的头
`rtk_coex.h`/`vendor_cmd.h`/`vendor_cmd_bt.h` + 只被 coex 用的 7 个 `gap_*.h`;`amebad_btgap` target、
`AMEBAD_BT_NO_BTGAP` option 一并去掉。

**为什么能删**:符号实测我们的 HCI 层对 btgap.a 的全部依赖只有 9 个——`bte_init`、4 个 coex 用的
`gap_*`/`le_get_*`、4 个通用工具 `os_delay`/`os_mem_free`/`os_mem_zalloc_intern`/`trace_log_buffer`;
`hci_uart.c`(串口收发)对 btgap **0 依赖**。处理:
- **`bte_init` → `hci_nostack.c::bt_hci_bringup_nostack()`** 直接调 `hci_tp_open()`——它本就是我们的代码,
  跑完整 bring-up(`hci_board_init`→`bt_reset`→`hci_process_table`:读版本/ROM、设波特、下 patch、
  HCI reset)。`bte_init` 原来只是替我们调它 + 起 host(我们不需要 host)。
- **4 个工具符号**在 `hci_nostack.c` 用 osif/FreeRTOS 重新实现(`os_mem_zalloc_intern`→`osif_mem_alloc`+
  memset、`os_delay`→`osif_delay`、`os_mem_free`→`osif_mem_free`、`trace_log_buffer`→空)。
- **4 个 coex `gap_*`/`le_get_*`** 随 coex 文件删除而消失(见下"WiFi/BT coex")。
- **踩坑**:`rx_ind` 在 UART RX **中断**里直接跑 `hci_tp_config`(含 patch 下载的重活)→ 中断栈溢出 →
  `Hard Fault CFSR=0x400 INVSTATE`(卡在"Set baudrate"后)。修法:`rx_ind` 改空操作(中断只填 rx ring),
  把事件重组 + `hci_tp_config` 放到**任务上下文**的轮询循环(`wifi_bringup_thread`,16 KB 栈)里跑。

实测:`BT INIT success` + `[BT] HCI bring-up OK (no btgap.a)`,bridge 上**五条裸 HCI 全部正常回应**
(Read Local Version 读出 normal patch 的 `0xBFE5`/`0x61EB`、BD_ADDR 94:C9:60:B7:0E:DB)。镜像
**小了 ~94 KB**(712704→**618496**)。组件收敛到 18 源 + 18 头,无 `sdk/` 子树、无 btgap.a。

## WiFi/BT coex

coex **算法不在 btgap.a 里**(实测 btgap.a 零 coex 符号),分三层:

1. **算法本体 = `lib_wlan.a`(WiFi 驱动侧)**:决策引擎 `hal_coex_run_case`/`rtl8721d_btcoex_run_case`、
   **TDMA 时隙表** `rtl8721d_pstdma_table`/`set_pstdma_cmd`/`set_coex_table`、PTA 仲裁
   `hal_coex_set_external/internal_pta`、`coex_hal_fun`、`Hal_EfuseParseBTCoexistInfo_8721D`。
2. **BT 侧 PTA = 控制器固件**(`rtlbt_fw` patch,patch 名里 `BTCOEX Version: 20151130-0202`)。
3. **host 胶水 = `rtk_coex.c`/`vendor_cmd.c`**(原 in-tree):把 BT 连接状态经
   `rltk_coex_mailbox_to_wifi`(→lib_wlan.a)喂给 WiFi 侧算法;btgap.a 在这只提供 GAP vendor 管道
   `gap_register_vendor_cb`/`gap_vendor_cmd_req`/`le_get_conn_param`/`le_get_gap_param`(**非算法**)。

**现状**:随 btgap.a 一起**移除了第 3 层(host 胶水)**——所以当前镜像不会把 BT 状态喂给 WiFi 侧算法。
第 1、2 层(算法本体 + 控制器固件)原封不动还在。对纯 DTM/HCI 测试无影响(`bt_dtm_bringup` 仍 `gnt_bt`
把 RF 判给 BT)。**换 NimBLE 做 WiFi+BLE 并发时**:NimBLE 需(a)提供等价的"发 vendor HCI + 取连接
参数",(b)把 BT 连接状态接回 `rltk_coex_mailbox_to_wifi`——仲裁本身一行不用动。

## 复现

```bash
# 配置 BT 测试镜像(在已有 build/km4 上加 flag)
cmake build/km4 -DAMEBAD_ENABLE_HCI=ON                    # normal(默认 BLE DTM)
# cmake build/km4 -DAMEBAD_ENABLE_HCI=ON -DAMEBAD_HCI_FW_MP=ON  # mp(RF 厂测固件)
# 全量构建 + 组装 Image_All.bin
python3 component/amebad/image_tool/build_image_all.py
# 烧录(COM29)
#   flash.ps1 -BootPort COM29 -FlashPort COM29 -SkipModeCheck -Image build/amebad/Image_All.bin
# 抓 boot log(bring-up 文本;镜像随后进 bridge,文本停)
#   sniff_log.ps1 -Port COM29 -Baud 1500000 -Seconds 30
# host 裸 HCI 驱动 bridge(main.c 已默认进 bridge,COM29 变 HCI 管道直到 reset):
#   project/amebad/hci_test.ps1 -Port COM29 -Baud 1500000   # sync 在 "entering HCI bridge" marker
```

回到默认 WiFi 镜像:`cmake build/km4 -DAMEBAD_ENABLE_HCI=OFF`。

## 关键文件

- 新增:`component/amebad/hci/`(**全平铺、无 btgap.a**:CMakeLists.txt、in-tree
  hci_nostack.c / hci_dtm.c / hci_fw_stub.c / hci_compat.c / hci_syscalls.c / build_info.h,vendored
  HCI 传输 10 源 + 两个 patch blob + 18 头,均直接在 `hci/` 下,无 `sdk/` 子树)、
  `project/amebad/hci_test.ps1`、`project/amebad/hci_info.ps1`(查 version/features)
  - 已删:`btgap.a`、coex `rtk_coex.c`/`vendor_cmd.c` + 头、36+10 个未引用的原厂上层栈 API 头
- 改:`component/amebad/CMakeLists.txt`(`add_subdirectory(bt)`)、
  `project/amebad/app_km4/CMakeLists.txt`(`AMEBAD_ENABLE_HCI` option + LINK_GROUP 追加
  + hci_syscalls.c 进 executable)、`project/amebad/app_km4/main.c`(`AT_PROJECT_HCI` 调用点)
- 不改:全局 `config/platform_opts.h`(CONFIG_BT 仍全局 0,只 `amebad_hci` 组件 scope 开 CONFIG_BT=1)
