---
name: reference_amebad_ble_host_km0_infeasible
description: 为什么 NimBLE host 搬不上 AmebaD KM0(UART1 KM4 独占 + KM0 128K/48K 预算装不下);含 IPC 机制速查
metadata: 
  node_type: memory
  type: reference
  originSessionId: 2c7314f2-7f7e-46e5-84f0-75d717be0da0
---

调研结论(2026-06):**把 [[project_amebad_nimble_bringup]] 的 NimBLE host 从 KM4 搬到 KM0
不可行**,用户拍板维持 host 在 KM4。两条硬约束:

**1. HCI 必须留 KM4(UART1 是 KM4 独占外设)。** BT 控制器走 UART1:基址 `0x4000A000`(KM4 域,
`0x40xxxxxx`;KM0 域是 `0x48xxxxxx`),`UART1_IRQ=35` 只在 KM4 向量表(KM0 的 LPIRQn 里没有),
控制器引脚焊死在 UART1 的 PA0/PA2/PA4。所以 KM0 根本摸不到 UART1 —— 想在 KM0 跑 host 只能
**HCI-over-IPC**:KM4 当哑中继(UART1 RX→IPC→KM0、KM0→IPC→UART1 TX)。coex 的 lib_wlan.a 也在 KM4。

**2. KM0 预算太小,塞不下 NimBLE host。** (`component/amebad/common_ld_script/rlx8721d_layout.ld`)
- flash `KM0_IMG2` = **128K**,现用 ~71K(WiFi FW blob lib_wifi_fw.a + shell + FreeRTOS)→ 剩 **~57K**。
- RAM `BD_RAM` = **48K**,现用 ~35K(含 20K `.ram_heap.data` 堆池)→ 剩 **~14K**。
- KM0 arch = `armv8-m.base`(M23 baseline,无 DSP/FPU;FreeRTOS 用 GCC_ARM_CM0 port 见 [[reference_amebad_freertos_v1062_ports]])。
- 对照 KM4:`KM4_IMG2`=868K。NimBLE host+GATT 在 KM4 链接进去 ~78K flash(HCI 基线 618496→带 GATT 696320),
  已超 KM0 剩的 57K;BLE Mesh 还要 +40K+。RAM 14K 也只够极简 mbuf。→ 完整栈装不下,极简 LE-only PoC 也是 flash 卡死。

**IPC 机制速查**(以后真要做 HCI-over-IPC / 跨核传数据时直接用):
- API:`ipc_send_message(u8 ch, u32 msg)` / `u32 ipc_get_message(u8 ch)`(`fwlib/rtl8721d_ipc_api.c`,
  `#include "ameba_soc.h"` 即可)。注册接收 handler:改 `usrcfg/rtl8721d_ipccfg.c` 的 `ipc_init_config[]`
  (KM0/KM4 各一份 block,`IPC_USER_POINT` 传指针 / `IPC_USER_DATA` 传 32-bit 立即数)。
- 已占用 ch:0=shell switch、1=wifi_fw、2=flashpg、3/31=km4_tickless;**ch 4-30 空闲**。
- 传指针要 DCache:发送端 `DCache_Clean`、接收端 `DCache_Invalidate`(M33 有 D-cache)。
- KM0 已在 main.c 挂 `InterruptRegister(IPC_INTHandler, IPC_IRQ_LP, IPCM4_DEV, 2)`;现成例子见 monitor 的
  shell switch、rtl8721dlp_km4.c 的 tickless。
