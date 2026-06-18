---
name: project_amebad_nimble_bringup
description: 在 component/amebad/hci(无 btgap.a)上接 Apache NimBLE 1.9.0 host 的 bring-up 计划与 P0 集成配方
metadata: 
  node_type: memory
  type: project
  originSessionId: 2c7314f2-7f7e-46e5-84f0-75d717be0da0
---

在 [[project_amebad_ble_dtm]] 做好的 HCI 传输(component/amebad/hci,已删 btgap.a)上接
**Apache NimBLE 1.9.0**(开源 LE host)做 GAP/GATT/SM + **BLE Mesh** + **WiFi/BT coex**。
RTL8721D 纯 BLE 控制器,NimBLE LE-only 契合。计划 plan 文件
`.claude/plans/component-amebad-clever-owl.md`;阶段 P0..P5(见 task 列表)。

**布局(用户定的)**:upstream submodule 在 `component/thirdparty/nimble`(apache/mynewt-nimble
@ `nimble_1_9_0_tag`);我们的 CMake 胶水层是独立组件 `component/nimble`(target 名 `nimble`),
不塞 thirdparty 也不塞 amebad/。top CMakeLists.txt 的 amebad 块里 add_subdirectory(component/nimble)。
build flag `AMEBAD_ENABLE_NIMBLE`(app_km4),与 `AMEBAD_ENABLE_HCI` 并列、共享控制器 bring-up
(amebad_hci + 固件 blob + hci_syscalls 进 executable);main.c 在 wifi_on 后 `#ifdef AT_PROJECT_NIMBLE`
调 `amebad_nimble_start()`。

**P0 已完成(builds + links,2026-06)**。集成配方/踩坑(非 Mynewt 用 CMake 编 NimBLE):
- **源/头清单**照 upstream `porting/nimble/Makefile.{defs,mesh,tinycrypt}` + freertos NPL。file(GLOB)
  upstream src 目录(porting/nimble/src、porting/npl/freertos/src、nimble/src、nimble/host/src+util+
  services/{ans,bas,gap,gatt,dis,ias,lls,tps}/src、host/store/ram/src、transport/src、host/mesh/src、
  ext/tinycrypt/src);我们 3 个胶水 .c 显式列。
- **syscfg**:component/nimble_config/include/syscfg/syscfg.h = 预定义 override(BLE_TRANSPORT_HS=native、
  **BLE_TRANSPORT_LL=custom**、BLE_MESH=1、SM_SC=1、BSP_SIMULATED=0)→ `#include_next <syscfg/syscfg.h>`
  拉 upstream 默认 → `#include "mesh_defaults.h"`。**我们的 include 目录必须排第一**(才 shadow + include_next)。
- **mesh_defaults.h**:upstream porting 默认 syscfg **不含** BLE_MESH_* 子选项(只在 mesh 示例里),
  从 `porting/examples/linux_blemesh/include/syscfg/syscfg.h` awk 抽 138 个 #ifndef MYNEWT_VAL_BLE_MESH 块。
  放 include_next **之后**(有的 mesh 默认引用非-mesh MYNEWT_VAL)。
- **transport 是 custom**(LL=外部控制器):实现 **`ble_transport_to_ll_cmd_impl`/`_acl_impl`**(带 `_impl`!
  public 名由 transport/monitor.h inline 转发)+ `ble_transport_ll_init`;RX 调 public `ble_transport_to_hs_evt/acl`。
  transport_amebad.c 要先 `#include "os/os.h"`(transport.h 用 os_mempool_put_fn)。
- **console shim**:core host(ble_hs.c 等)无条件 `#include <console/console.h>`,只用 console_printf。
  自备 component/nimble_config/include/console/console.h:`#define console_printf(f,...) printf(f,##...)`(printf=
  hci_compat.c 的 wrapper)。
- **NPL in_isr 要 CMSIS SCB**:npl_os_freertos.c `#include NIMBLE_NPL_OS_EXTRA_INCLUDE` → CMake
  `-DNIMBLE_NPL_OS_EXTRA_INCLUDE="ameba_soc.h"`。
- **排除**(host-only,照 linux 示例 NIMBLE_IGNORE):porting/nimble/src/{hal_timer.c(要 nrfx.h)、
  os_cputime.c、os_cputime_pwr2.c};mesh CLI/shell/example:host/mesh/src/{shell,testing,light_model,
  model_cli,model_srv}.c(要 console+shell)。services/bleuart 不 glob。
- 编译:target -w(压 upstream warning,盖全局 -Werror);link FreeRTOS + amebad_hci + amebad_sdk_includes
  + amebad_wlan。libnimble.a ~937KB;全 image link 零未解析、655360 B(HCI 基线 618496,+~36KB gc 后)。

**P1+P2 已完成(2026-06,实机 host synced ✅)**:NimBLE 1.9.0 在控制器上跑通,btgap.a 彻底被开源栈取代。
- transport(component/amebad/nimble_port/transport_amebad.c)照 upstream **`nimble/transport/uart_ll`**(LL-side H4)写:
  - **复用 NimBLE 的 H4 状态机** `nimble/transport/common/hci_h4/`(要把 `hci_h4.c` 加进 glob + include
    `transport/common/hci_h4/include`)。`ble_transport_ll_init` 里 `hci_h4_sm_init(&sm, &hci_h4_allocs_from_ll, frame_cb)`;
    frame_cb:EVT→`ble_transport_to_hs_evt`、ACL→`ble_transport_to_hs_acl`。H4 类型:CMD=0x01/ACL=0x02/EVT=0x04。
  - **RX**:FreeRTOS task 轮询 `hci_uart_recv`(ring,hciuart_irq 填)→ `hci_h4_sm_rx`。重组在 **task** 不在 IRQ。
  - **TX**:`ble_transport_to_ll_cmd_impl`(len=3+buf[2])/`_acl_impl`(OS_MBUF_PKTLEN)→ H4 前缀 + `hci_ctrl_putc`
    (新加到 hci_uart.c:阻塞单字节写 UART1 = while(!UART_Writable)UART_CharPut;让 NimBLE TU 不用 include ameba_soc.h)。
- **启动顺序**(nimble_app.c amebad_nimble_start):① `bt_dtm_bringup()`(gnt_bt+trace+hci_tp_open,控制器 patch+reset)
  → ② `nimble_port_init()`(sysinit 调 ble_transport_ll_init 初始化 h4sm)→ ③ `nimble_amebad_transport_start_rx()`
  → ④ `nimble_port_freertos_init(host_task)`。host 发 HCI Reset+读特性 经 transport 往返 → sync_cb。
- 实机:`[NimBLE] host synced — controller ready`。image 692224 B。

**P3 device 侧已完成(2026-06)**:nimble_app.c 在 sync_cb 里起 connectable 广播 "amebad-nimble" +
注册 ble_svc_gap_init/ble_svc_gatt_init + 一个示例 128-bit service(svc ...2d / chr ...2e,读写 1 字节,
ble_gatts_count_cfg+add_svcs)。控制台 `advertising as 'amebad-nimble'` 确认。boilerplate 照 apps/bleprph:
ble_hs_util_ensure_addr + ble_hs_id_infer_auto(own_addr_type)→ ble_gap_adv_set_fields(name)→
ble_gap_adv_start(...,nimble_gap_event)。gap event:DISCONNECT/ADV_COMPLETE 重新广播。image 696320 B。
**已实机验通(bleak,over-the-air)**:`project/amebad/nimble_ble_verify.py`(bleak,Windows 宿主跑)
scan→connect→discover→read 00→write 0xA5→read-back a5 = **PASS**。BD_ADDR 94:C9:60:B7:0E:DB(与 DTM 时读的一致)。
**验证工具链坑**:bleak 用 Windows Python(WSL2 无 BT 透传),`powershell.exe -Command "python ..."` 跑;
路径用**正斜杠 UNC** `//wsl.localhost/Ubuntu-24.04/...`(双引号 -Command 里 `\\` 会被 zsh 吞成 `\` → UNC 路径翻倍;
脚本 docstring 别写反斜杠路径,`\U`/`\x` 会被当 unicode 转义报错)。Windows 装 bleak:`python -m pip install bleak`。

**P5 coex 已完成 + 并发验证(2026-06)**:比预想简单太多——
- **coex 自动就绪**:我们 vendored 的 `hci_normal_start()`(hci_process.c:40,每次 bring-up 第一步调)已经
  `rltk_coex_bt_enable(1)` 并创建 WiFi 侧 pcoex。**别再调第二次**(报 `[rtw coex error] pcoex has been created`)。
- **lib_wlan.a 不需要 host 的 mailbox_to_bt**(nm 实测无此 U 符号)——WiFi↔BT 走片上 HW mailbox
  (lib_wlan rtw_coex_mailbox_thread)+ 控制器固件,**host 不用注入 vendor HCI**,跟 NimBLE 的 HCI 流控零冲突。
  (对比老 btgap rtk_coex.c 走 gap_vendor_cmd_req 注入,这里完全不用。)
- coex_amebad.c:`amebad_nimble_coex_init` 只记日志(enable 已自动);`amebad_nimble_coex_set_conn` =
  `rltk_wlan_btcoex_set_bt_state(1/0)`,挂在 nimble_gap_event 的 CONNECT/DISCONNECT。
- main.c 的 AT_PROJECT_NIMBLE 块改成**不 early-return、fall through 到 wifi_connect+iperf**,让 WiFi+BLE 并发。
- **并发实测**:WiFi iperf **11.8 Mbit/s**(满速,21.1MB/15s,iperf -s 日志)**同时** BLE soak(held 连接 30s,
  93 次读写 0 失败,nimble_ble_soak.py)→ 两边都满性能,互不饿死。**coex 验通**。
- 天线:目前用 bt_dtm_bringup 的 gnt_bt(PTA_BT),并发下 WiFi 仍满速(coex 仲裁覆盖);若以后 WiFi 被饿可
  改 PTA_AUTO。验证工具 project/amebad/nimble_ble_{verify,soak}.py(Windows bleak)。

**SM crypto 换 wolfSSL 已完成(2026-06,实机 KAT 全 PASS)**:NimBLE 1.9.0 的 `ble_sm_alg.c`
**写死 tinycrypt**(无 `BLE_CRYPTO_STACK_*` 开关),故不改上游,而是新增胶水 `component/nimble_config/wolfcrypt_sm_glue.c`
重实现它(+mesh)调的那几个原语,底层接 wolfCrypt(component/wolfssl_config v5.9.1):
- **AES-128 单块**:`tc_aes128_set_encrypt_key`/`tc_aes_encrypt` → `wc_AesSetKeyDirect`/`wc_AesEncryptDirect`
  (要在 user_settings 开 `WOLFSSL_AES_DIRECT`)。**tinycrypt cmac_mode.c 保留**,内部调 tc_aes_encrypt → AES-CMAC(f4/f5/f6/g2)自动走 wolfCrypt。
- **ECDH P-256**:`uECC_make_key`/`uECC_shared_secret`/`uECC_valid_public_key`/`uECC_set_rng` → `wc_ecc_*`。
  私钥用 NimBLE 的 RNG(uECC_set_rng=控制器 HCI LE_Rand,强);wolfCrypt WC_RNG 只做 ECC_TIMING_RESISTANT blinding。
  格式 big-endian(micro-ecc 风格)X‖Y/d/secret,对 wolfCrypt unsigned-bin 1:1。
- CMakeLists REMOVE_ITEM 撤 `ext/tinycrypt/src/{aes_encrypt,ecc,ecc_dh,ecc_dsa,ecc_platform_specific}.c`;
  `curve_secp256r1` 是 tinycrypt/ecc.h 里的 static const,引用 double_jacobian_default/x_side_default/
  vli_mmod_fast_secp256r1(本在 ecc.c)→ glue 给 3 个**空桩**满足链接(我们绕开曲线结构体、永不调)。
- 接线:`amebad_wolfssl` 进 app_km4 的 cross_refs **LINK_GROUP**(不能在 nimble PUBLIC-link,会和 FreeRTOS 成环);
  nimble 用 `$<TARGET_PROPERTY:amebad_wolfssl,INTERFACE_*>` 借头+宏(无链接边)。KAT `amebad_nimble_sm_crypto_selftest()`
  (AES-ECB FIPS-197 + AES-CMAC RFC4493 + ECDH 对称性 + **BT 规范 2.3.5.6.1 样本 DHKey** + pubkey validate)实机 ALL PASS。

**血泪大坑(布局敏感的链接即崩,排查 ~45 轮)**:`uECC_make_key` 导出公钥**必须用 `mp_to_unsigned_bin_len(key->pubkey.x/y,…)`,
绝不能用 `wc_ecc_export_public_raw`**。后者经 `wc_export_int`(wolfmath.c),它引用 hex/十进制 stringify `sp_tohex`/`sp_radix_size`;
**仅仅把这段 hex 转换代码*链接*进 KM4 image(根本不运行),就让 KM4 在 app_start 里 main() 之前确定性挂死**
(把后续 XIP 函数挪几百字节、撞上某个潜伏故障点;同尺寸 4K-padding 下 export-linked 挂、export-not 通,故非大小)。
排查法:**bleak 扫到广播=确定性"启动成功"信号**(KM4 早期 LOGUART 是 KM0/KM4 抢占的**非确定性**输出,marker/serial 全不可信,白费很多轮);
最终靠 shim 函数逐个空壳化二分 + ld `--cref` map(确认非 psram/大小/newlib/符号翻转)。
**机理已证(非猜)**:① 末尾塞 2KB .rodata(同最终尺寸、但 app_start 不挪)→ 照样 boot ⇒ 不是大小、是**位置**;
② ld 里把 `.text.app_start` KEEP 钉到 XIP .text 起点(0x0e000040 实测生效)+ 恢复 export → **仍挂** ⇒ 挂的**不是 app_start 本体,是它某个 callee**(在 fwlib、随 export 挪位,甚至可能是 BOOT REASON 之前的早期 callee);
hang/boot 两版**所有 RAM section 字节级相同,只 XIP .text 末尾差 0x3F0**。整条 boot 路径 callee 散在 fwlib 没法都钉;精确到哪个 callee/哪个坏地址只能 **SWD halt 读 PC**。同类见 [[reference_amebad_boot_rodata_xip]]。
**实用结论**:`mp_to_unsigned_bin_len` 修复(不引入那 1KB)干净可用;但潜伏 bug 还在,以后 app_start 之前再多 ~1KB 代码(如 P4 mesh)可能复发。

**下一步**:只剩 P4 mesh(bt_mesh 配网+示例 model;syscfg BLE_MESH 已开;mesh src 已编进 lib;mesh 也用上面的 uECC/AES shim)。
