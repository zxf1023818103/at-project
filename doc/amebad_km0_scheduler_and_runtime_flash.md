# AmebaD (RTL8721D) — KM0 调度器修复 & SPE 运行时写 Flash 的限制

两件在 `make AWS=ON`（TF-M 安全存储 + AWS IoT demo）排查中纠缠在一起的事：
**(A)** KM0 的 FreeRTOS 调度器在本 build 从来没起来过（pre-existing，KM4-independent）；
**(B)** 运行时把大块凭据写进 TF-M Protected Storage（`set key` / `set cert`）会卡死，
只有 ≤48 B 的小写能成。两者都用 OpenOCD 实机钉过。

> **状态（2026-06-15）**
> - (A) **✅ 已修**：KM0 关掉 `configUSE_TIMERS` → `xSchedulerRunning=1`，KM0 跑起来了。
> - (B) **✅ 已修（2026-06-15）：把 SPE flash 驱动挪进 SPE RAM**。根因=AN0400 §18.2——操作 SPIC 的 program/erase 代码必须从 RAM 跑，不能从 XIP flash 跑（flash op 占着 SPIC 时设备没法服务 XIP 取指）。`Driver_Flash.c` 的 `ARM_Flash_ProgramData/EraseSector` 加 `.ramfunc`（`RAMFUNC` 宏 + helper `always_inline`），`region_defs.h` 定义 `S_RAM_CODE_START/SIZE`（从 128 KB 安全 SRAM 顶切 8 KB），TF-M `tfm_common_s.ld` 的 `.ER_CODE_SRAM` 段就把它们放进 `CODE_RAM`、CMSIS startup copy-table 开机拷过去。**实测：运行时 256/768/1200/1700 B ITS 写全 `status=0` OK，8× 1700 B 连写零故障、telemetry 不断**（之前任何 ≥256 B 运行时写都 secure-LOCKUP 硬死板）。残留：偶发一次可恢复的 NS HardFault（写仍成功、系统存活），压测里没复现。私钥改回**直接存 PEM**（1.7 KB < 2 KB ITS cap，省掉 DER 的 `mbedtls_pk_parse_key`——那个会爆 CLI 任务栈）。**唯一剩的口子=`set key` 把 PEM 流过控制台时丢字节**（WiFi 任务抢占 CLI 任务→小 RX FIFO 溢出→EOF 丢），是 host 工具/集成问题，与本 flash 修复无关。**(以下 B′ 段是修复前的诊断历程，保留。)**一句话：**SPE 的 flash 代码本身没问题**（boot 单线程下 256 B replace + 1200 B compaction 全 PASS — `tfm_storage_selftest.c::stg_test_big`），**这是纯运行时环境故障**。原始症状是 SPE 内 **secure 故障→LOCKUP**（CFSR=0x400 BusFault IMPRECISERR + SFSR=0x10 SecureFault INVTRAN → HFSR FORCED → 双故障锁死，PC=0xEFFFFFFE；KM0 此时**正确地** parked 在 wfe，与卡死无关）。`__DSB`（Driver_Flash.c）排掉 secure 锁死后，暴露一处 **NS 侧 RAM 码指针被写坏**（有效 0x0E… 码地址 bit25 被清成未映射 0x0C… → NS HardFault）；再加 `Cache_Flush`（I+D clean+invalidate，对齐 SDK）**不解决** → 不是 cache，是真·RAM 写坏；再叠 KM0 quiesce（`FLASH_Write_Lock`）能消掉那处 RAM 腐败（不再 HardFault），但它在 KM0 tickless 深睡下等 `BIT_KM0_SLEEPSYS` 等不到 → 静默卡死。**每个 mitigation 消一层、露下一层**。结论：运行时 secure 写要 Realtek SPIC/RSIP 内部细节（NDA AN）才能根治；**实务走 boot 期 provisioning（已证可行）或保持内嵌 key**。AWS demo 仍用内嵌 RSIP 加密凭据，端到端 CloudWatch 已验。
> - (C) **✅ KM0 quiesce 翻案（2026-06-25）：wfe-free RAM-spin 版跑通**。旧 ch2 quiesce 判死的根因 = KM0
>   handler 里的 `wfe`（深睡上下文恢复 HardFault）。换成 **私有 ch4 + KM0 在 RAM-resident handler 里自旋**
>   等 KM4 清请求寄存器（不 `wfe`，故无睡眠/恢复上下文可坏），KM4 侧轮询 KM0 的 ack 寄存器 **带 timeout**
>   （KM0 不响应就继续，永不死等）。握手走一条直连 `IPCx_USR[4]` 寄存器（KM4 写 `IPCM4_DEV` 当请求、KM0 写
>   `IPCM0_DEV` 当 ack，KM0 直接写自己寄存器不触发 KM4 IRQ）。代码 `spe_flash_park_ipc_int` /
>   `spe_km0_park`/`spe_km0_unpark`（`rtl8721d_flash_ram.c`）+ ch4 注册（`rtl8721d_ipccfg.c` KM0 表）+
>   `tfm_storage_lock.c` 只在 AWS key 写时调。**实测：连线态重写 1193B key 8/8 干净**（旧版同场景 0/3），
>   KM0 console 仍活、AWS telemetry 不断 → **RSA-size 运行时安全写现在可靠了**，不再只能 ECC-32B / boot-期。
>   - 代码改动取舍：Driver_Flash.c `__DSB`+`Cache_Flush`（对齐 SDK，正确、改善 boot 健壮性，**保留**）；`stg_test_big`（boot 期大写回归证明，**保留**）；`tfm_storage_lock.c` 的 `FLASH_Write_Lock` 已**回退**（运行时静默卡死，比可见 NS fault 更难调）。

相关：[`amebad_tfm_storage.md`](amebad_tfm_storage.md)、[`amebad_xip_layout_boot_hang.md`](amebad_xip_layout_boot_hang.md)、
`AN0400 Ameba-D Application Note v12` §18（Flash Operation）/§12（IPC）、memory `project_amebad_aws_iot`。

---

## A. KM0 调度器从来没起来（已修）

### 症状
`make AWS=ON` 烧录后，KM4 一切正常（连 AWS、发 telemetry），但 KM0 侧功能（双核
CLI 的 `km0` 那半、PMU tickless）不工作。OpenOCD 读 KM0：

| 量 | 值 | 含义 |
|---|---|---|
| `xSchedulerRunning` | `0` | 调度器没起来 |
| `uxCurrentNumberOfTasks` | `4` | 任务创建过（堆 OK） |
| KM0 PC | ROM `0x49a2`（`b .` 自旋，thread mode） | 卡死在 ROM |

**换干净 safe 镜像（正常连 AWS 的那版）读 KM0 也一样** → 这是 pre-existing，跟 AWS /
任何改动无关。

### 根因 = KM0 堆耗尽（OpenOCD tcl 单步 unwind 追踪钉死）

**真相：不是内存腐败，是堆不够。** 用 OpenOCD tcl 脚本（`scripts/km0_trace.tcl`，server-side
循环逐条单步 + 读 PC/SP，比 telnet 单步稳）把崩前那段 PC/SP 全打出来，看到一条**干净的整栈
退栈**：
```
mm_unlock_restore → … → xTaskCreate → vTaskStartScheduler(0xc000654..672)
  → 返回 main(0xc0002d0) → 返回 app_start(0xc00f084) → 返回 bootloader(0x08000548)
  → ROM 0x498a → 0x49a2 (`b .` 死循环)
```
**`vTaskStartScheduler` 居然 return 了** —— 而它在 FreeRTOS 里**只有当某个任务创建失败
（pvPortMalloc 返回 NULL）才会 return**。timers ON 时,**软件定时器任务+命令队列**把 KM0 的
20 KB ucHeap 撑爆 → `xTimerCreateTimerTask` 的 malloc 拿不到内存 → `vTaskStartScheduler`
返回 → 整栈一路退到 boot ROM → `0x49a2` 死循环。

之前那些误判全在这里纠正：
- **`0x49a2` 不是"ROM 时钟自旋"**,就是 **main() 合法返回进 boot ROM** 的地方（栈上 `0x498b`
  是 main 的真实返回地址,一直都对,不是被写坏的垃圾）。
- crash 在 `vTaskStartScheduler` **内**、`xPortStartScheduler` **之前** —— 因为它压根没走到
  启动调度器那步,在 timer 任务创建失败后就 return 了。
- 端口没问题（`GCC_ARM_CM0` 正确）；不是中断（`cpsid i` 屏中断后照崩）；不是 tlsf_config 腐败。

### 修复（真·治本，KM0-only）
`component/freertos-kernel_config/FreeRTOSConfig.h` KM0 块,把堆从 20 KB 提到 **28 KB**：
```c
#define configTOTAL_HEAP_SIZE   ( ( size_t ) ( 28 * 1024 ) )
```
ucHeap @ ~`0x87440`,BD_RAM 到 `0x8F000` → 约 31 KB 天花板,28 KB 安全。**验证：timers ON 下
KM0 `xSchedulerRunning=1`、PC 在自家代码跑、用 PSP；整机 KM4 AWS 照常连+发 telemetry。** 软件
定时器恢复可用（不再需要 `configUSE_TIMERS=0` 治标）。

> **走过的弯路（留个记录）**：先误以为是 tlsf_config/TLSF 直接写坏 MSP 返回槽 `0x8ffc0`,试过 KM0
> 换 heap_5（idle 就 HardFault）、删 ROM-BSS 复用堆区（没用）、`cpsid i` 屏中断（没用）、
> `configUSE_TIMERS=0`（治标能跑）。直到把整段退栈打出来才看清是 `vTaskStartScheduler` return
> = malloc 失败 = 堆耗尽。**教训：scheduler-start 崩 + 一路退到 boot ROM,先查 heap free,别一头扎进"返回地址被写坏"。** `scripts/km0_trace.tcl` 的 server-side 单步追踪是钉死它的关键工具。

---

## B. SPE 运行时写 Flash 的大小限制

> ★★**翻案（2026-06-15）：运行时大写卡死的真因是 NS interface 没串行化,不是 KM0 XIP 争用。**★★
> 切到串行化的 NS interface 后,**32→768 B 运行时 PS 写全部干净成功**(~130-300 ms),≥1024 B 是
> **干净的 `-132` 配置上限**(不是 hang)。下面 B 节早先关于"≥64 B hang / KM0 跨核 XIP 争用 /
> ch2 quiesce"的分析**全部作废**,正确结论见本节末"★真因 + 修复"。早先内容留作排查史。

TF-M 的 SPE（KM4 secure world）用 ROM `FLASH_TxData*` 在 auto/XIP 模式写 SPI flash
（`Driver_Flash.c`，无 `FLASH_Write_Lock`）。**Boot 期小写成**（`[STG] PS persistence PASS`
= 32 B），**运行时大写"失败"**（实为下述并发踩栈,当时误判成 XIP）。

### 阈值（`pstest` 命令实机扫，NS 侧 `psa_ps_set`）
| 写入 | 结果 |
|---|---|
| 32 B | ✅ OK（~124 ms） |
| 48 B | ✅ OK（~148 ms） |
| **≥64 B** | ❌ **hang**（SPE flash 写自旋不返回，console 卡死，需 reflash 恢复） |

注意是 **hang（不返回）不是返回 error** → NS 层"失败重试"救不了（永不返回）。

诊断命令：`component/amebad/app_km4/aws_cli.c` 的 `pstest <size> [retries]`；
host 扫描脚本 `project/amebad/pstest_sweep.ps1`。

### KM0 跨核仲裁这条路（曾以为是出路，结论复杂）
- AN0400 §18.1：SPIC 的 **auto 模式（XIP 取指）和 user 模式（program/erase）不能同时用**；
  §18.2：编程的核要把另一个核 **IPC-睡眠**（`FLASH_Write_Lock` → ch2 `FLASH_Write_IPC_Int`
  → 对核 `wfe`）。
- `device_mutex_lock(RT_DEV_LOCK_FLASH/CRYPTO)`（`tfm_storage_lock.c` 经 `--wrap` 包住所有
  NS `psa_*_set`）**管不了 KM0** —— 它只是 KM4 单核的 FreeRTOS 互斥锁，KM0 不参与。
  **实测确认**：当前镜像就握着 `device_mutex_lock(RT_DEV_LOCK_FLASH)` 跑 SPE flash 写,
  `pstest 32→OK(129ms)、48→hang`,**握着这把 NS 锁照样 ≥48B 卡死**。
  > **关于 `secure_set_ns_device_lock`（Realtek SDK secure-mbedTLS 的"SPE 回调 NS 拿锁"机制）**：
  > 我们上游 TF-M 没接它,但它做的事 = SPE 在 flash 写时回调 NS 的 `device_mutex_lock(RT_DEV_LOCK_FLASH)`,
  > **跟上面这把 NS 侧已经握着的同一把锁、同样覆盖那次 flash 写,效果完全一样** —— 它仲裁的是
  > "SPE ↔ NS 任务"(都在 KM4)的争用,管不了跨核 KM0;而且我们 TF-M 是 SFN 模型、secure call 期间
  > NS 整个挂起,根本没 NS 任务在跑。所以 `secure_set_ns_device_lock` 对这个 >48B 卡死无效（已用等价
  > 实测证伪）。
- `FLASH_Write_Lock`（IPC 睡 KM0）才能管 KM0，但需要 KM0 注册 ch2 handler。**之前一度
  以为"注册 ch2 崩 KM0"**——后用 RAM-marker 实验翻案：marker 没动 = arm 任务根本没跑，
  真因是 **A 节的 KM0 调度器没起来**（KM0 不跑 IPC → 不响应 ch2 → KM4 的 `FLASH_Write_Lock`
  自旋死等）。
- **A 修好后 KM0 活了，XIP 争用才真正成立**（KM0 死在 ROM 0x49a2 时压根没 XIP-fetch SPI
  flash）→ 见下"回归测"。

### 回归测：KM0 修好（活了）后阈值变差，坐实 XIP 争用
| 写入 | KM0 死（A 修之前） | **KM0 活（A 修之后）** |
|---|---|---|
| 32 B | OK | **OK**（127 ms） |
| 48 B | OK | **HANG** |

KM0 死在 ROM `0x49a2` 自旋时**不取 SPI flash 指令**，所以 48 B 能挤过去；KM0 活了之后
真的在 XIP-fetch 共享 SPI flash，48 B 就撞 hang —— **正是 AN0400 §18.1 描述的 auto/user
模式互斥**。→ 运行时只剩 **32 B** 稳。**双刃**：① 运行时大写更不可能（ECC-32B 成了唯一
runtime PS 路）；② 但 KM0 活了之后 **`FLASH_Write_Lock` 的 ch2 IPC-睡-KM0 机制第一次变得
可能可用**（之前 KM0 死，注册 ch2 也没用；现在 KM0 有调度器 + IPC 能收 ch2 去 `wfe`）——
若 ch2 quiesce 真能跑通，大写（乃至 RSA）就能成。

### ch2 quiesce 在 KM0-活前提下重测 —— 仍不可行（踩三道连环坑）
KM0 调度器修好后重试"arm ch2 + NS `FLASH_Write_Lock`"：
1. **从 FreeRTOS 任务里 arm ch2** → 多创建一个任务就**把调度器起步崩溃重新触发**了（`xSchedulerRunning=0`，回到 ROM `0x49a2`）。底层那个 ROM-时钟自旋 bug 是**堆/malloc 序列敏感**的，关 timer 只是恰好躲过，加个任务又踩中。
2. **改成在 `main()` 里直接 arm（不建任务）** → 调度器活了（`xSchedulerRunning=1`），KM4 也确实发了 ch2、KM0 也收到并派发了 `FLASH_Write_IPC_Int`，但 **KM0 当场 HardFault**（`Handler HardFault`，栈里压的 PC = **`0xa5a5a5a5`** = FreeRTOS 栈填充值）。`FLASH_Write_IPC_Int` 本体干净（`mrs/cpsid/wfe/bx lr`），崩在 **KM0 IPC-ISR 的栈路径返回到 0xa5a5a5a5 垃圾地址**。
3. **KM0 堆修好后又重测一次（healthy scheduler）→ 还是同款 HardFault**。读了崩溃栈帧：faulting
   **PC=`0xa5a5a5a5`、xPSR=`0x00000001`（T-bit 清零！）**，整个被中断的寄存器组都是栈填充
   `0xa5a5a5a5` —— 即 ch2 IPC 触发/返回那一刻 KM0 在一个**完全损坏的上下文**里。`FLASH_Write_IPC_Int`
   本体干净（`mrs/cpsid/wfe/bx lr`），所以坏在 **KM0 的 `wfe`-睡眠 / 上下文恢复路径**：KM0 一进
   这个 quiesce 的 wfe 就 HardFault 而不是干净睡下去 → 永不置 `BIT_KM0_SLEEPSYS` → KM4 自旋死等。
   **跟堆/调度器无关（修好后照崩）= 是 KM0 SOCPS/cache 跟那个 vendored flash-lock 的深层交互**，
   在 driver-only build 里跑不通。
- **结论**：ch2 quiesce 判死（KM0 wfe-睡眠上下文损坏，非堆/调度器问题）。**运行时进安全存储的干净
  路 = ECC-32B**（P-256 私钥 32B,已验证运行时 PS 小写能成,根本不碰 KM0/ch2;RSA-1.7KB 不行）。
  要救 ch2 得啃 KM0 SOCPS deep-sleep ↔ flash-lock wfe 的上下文保存/恢复（独立大活,大概率要 Realtek）。

### 干净出路：缩小秘密到 ≤48 B（早先结论，已被下面 ★真因 推翻）
- AWS IoT 支持 **ECC P-256**，私钥正好 **32 B**。
- RSA-2048（私钥 ~1.7 KB）：当时以为双重不可行。

### ★真因 + 修复（2026-06-15，正确结论）
顺 `configENABLE_TRUSTZONE` 那条线查 secure-context/串行化 → 发现我们的 TF-M NS interface 是
**`tfm_ns_interface_bare_metal.c`(无锁,不串行)**。SFN 模型下所有 partition **共用一份安全栈**,
两个 NS 任务同时进 secure call 就互踩 → **`pstest` 的 `psa_ps_set` 跟另一个并发 PSA 调用(极可能是
mbedTLS 熵种 NV-seed 走 ITS)撞车 → 安全栈损坏 → hang**。写得越大耗时越久,越容易撞上,所以"size-
dependent"(32 B 快、撞不上;48 B+ 容易撞)。**`dead-vs-alive` 是 confound**(KM0-dead build 调度器
坏、KM4 侧时序不同凑巧没撞),把我引去 KM0-XIP/ch2-quiesce 全是弯路。

**修复 = 换串行化 NS interface**：
- `tfm_ns_interface_rtos.c`(每次 PSA dispatch 包一对 mutex)替掉 bare-metal；
- 自写 FreeRTOS os_wrapper:`component/amebad/app_km4/tfm_os_wrapper_freertos.c`
  (`os_wrapper_mutex_create/acquire/release` over `xSemaphore*`,NULL handle 容忍 = boot 期单线程免锁)；
- `app_km4/main.c` 里 pre-scheduler 调 `tfm_ns_interface_init()`；
- CMakeLists 把 `tfm_ns_interface_bare_metal.c` 换成上面两个。

**实测阈值(串行化后)**：

| 写入 | 之前(bare-metal) | **现在(rtos 串行)** |
|---|---|---|
| 32 / 48 / 64 / 256 / 384 / 512 / 640 / **768 B** | 48B 起 hang | **全 OK**(~130–300 ms) |
| ≥ 1024 B | hang | **干净 `-132`**(PSA_ERROR_GENERIC_ERROR,5 ms) |

- **≤768 B 运行时进 PS 干净可写** → ECC P-256 的 key(32B)+ cert(~600-700B)运行时都能写进安全存储了。
- **≥1024 B 是 `-132` 配置上限**(不是 hang,不是争用):SFN partition 共享栈 / PS 的 CCM staging 缓冲
  装不下 → 5ms 内就返错。要 RSA-1.7KB,需抬高 SFN 共享栈 / PS asset/staging 配置(`app_km4_secure`
  的 TF-M config,代价=多吃 secure RAM),独立一项。
- boot STG 自检 + AWS 全链路实测**不受影响**。

> 教训:TF-M NS 侧用 bare-metal dispatch = 把 PSA 串行化甩给 app;只要有第二个 PSA 调用者(熵/任何
> 服务),就会跟你的写撞栈。**TF-M + FreeRTOS 默认就该用 `tfm_ns_interface_rtos.c`**(它对应 SDK 那套
> `configENABLE_TRUSTZONE` 安全上下文保护的位置)。

---

## 工具 / 寄存器 / 符号速查

- **OpenOCD gated-单步法**（钉 KM0 早期 crash 的关键技巧）：在 KM0 `vTaskStartScheduler()`
  前插 `while(g_km0_go==0) DelayMs(10)`，OpenOCD `mww <g_km0_go> 1` 释放 —— 彻底去掉
  attach 时序压力（DelayMs 定时窗口因校准不准 + 工具间延迟全错过）。配 `scripts/amebad_openocd.cfg`
  + `amebad-openocd-debug` 技能。**活自旋的 hang 可 examine + 断点（跟 boot-wedge 的 AP 门控两回事）**。
- **boot-vs-hang 判据**：读 `xSchedulerRunning`（=1 调度器活）/ `uxCurrentNumberOfTasks`（小值=正常，garbage=.bss 没清）。
- `FLASH_Write_Lock` 反汇编扒出：**`REG_LP_KM0_CTRL = 0x48000204`，`BIT_KM0_SLEEPSYS = bit4 (0x10)`**
  （KM4 自旋等 `mem[0x48000204] & 0x10`）。
- 烧录：`flash.ps1 -BootPort COM14 -FlashPort COM3 -Image build/amebad/firmware.bin`；console COM3 @ 1.5 Mbps。
