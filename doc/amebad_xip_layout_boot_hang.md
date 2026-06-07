# AmebaD (RTL8721D) KM4 image2 — 链接布局敏感的 boot 挂死

**结论先行**:往 KM4 image2 里**链接**某些代码(哪怕该代码 boot 时根本不执行),会把 XIP `.text`
里 `app_start` 及其后的整条 boot 函数链**向上挪几百字节~1KB**,挪到一个**从 XIP 执行就会出错**的
位置,导致 **KM4 在 `app_start` 里、`main()` 之前确定性挂死**(BLE 永远不广播、串口在 KM0 的
`RRAM:` 之后静默)。这是一个**地址/位置敏感的潜伏 boot 故障**,和 [[reference_amebad_boot_rodata_xip]]
同类。**触发器不是某个函数有魔力,纯粹是它的体积把 boot 路径顶过了界。**

首例:NimBLE 的 SM crypto 换 wolfSSL 时,`uECC_make_key` 用 `wc_ecc_export_public_raw` 导出公钥
→ 拉进 `wc_export_int`(wolfmath.c)→ 引用 hex/十进制 stringify `sp_tohex`/`sp_radix_size`(~1KB)
→ KM4 boot 挂死。**修复:导公钥改用 `mp_to_unsigned_bin_len(key->pubkey.x/y,…)`**(纯 unsigned-bin
路径,不碰 hex),不引入那 1KB → 正常启动。详见 [[project_amebad_nimble_bringup]]。

---

## 1. 症状

KM0 正常打印到 `RRAM: c0080 176B`(`rtl8721dlp_flashclk.c` 的 flash 高速 init),随后 `km4_boot_on()`
启 KM4 —— **KM4 一句都不打**(连 `app_start` 第一句 `KM4 BOOT REASON`、`main()` 第一句
`[KM4] at-project image2 …` 都没有),也**没有 HardFault dump**。BLE 不广播。

KM4 bootloader(`bootloader_km4/boot_ram_hp.c`)的 image2 签名校验(读 `__image2_entry_func__ +
sizeof(RAM_START_FUNCTION)` 处是否 == `"RTKWin"`)**通过**,`RamStartFun`(=`app_start`)指针**有效**——
即 bootloader 成功跳进了 image2,挂死在 `app_start` 执行途中(`main()` 之前)。

> KM4 bootloader 的 `DBG_*` 不在我们抓的 LOGUART 上(baseline 也看不到 `IMG1 ENTER RAM`),所以
> 签名失败的死循环也是静默的——**不能**靠"没看到错误"判断它过没过签名校验。

---

## 2. 机理(已证明,非猜测)

对比挂(HANG)与正常(OK)两份镜像:**所有 RAM section(`.ram_image2.data/.bss/.nocache`、heap、
entry、validate)字节级完全相同;`.init_array` 两边都空(无 C++ 全局构造器);唯一差别是
`.xip_image2.text` 末尾差 `0x3F0`(1008 字节)** —— 就是被链接进来的那段代码。

三个判别实验(都用 **bleak 扫到广播 = 确定性"启动成功"** 判定,见 §4):

1. **二分**:把 `uECC_make_key` 里那一句 `wc_ecc_export_public_raw` 去掉(其余不变)→ **boot**。
   ⇒ 就是这 ~1KB 代码被链入触发的。
2. **末尾 pad**:保留修复版,在所有 `.text` 之后塞 2KB 哑 `.rodata`(最终 4K-padding 尺寸 729088,
   **和挂的版本一样大**,但 `app_start` 不挪位)→ **boot**。
   ⇒ **不是镜像大小/总 XIP 大小**,是**位置**。
3. **ld 钉死 app_start**:`.xip_image2.text` 里 `KEEP(*(.text.app_start))` 放最前,把 `app_start`
   固定到 `0x0e000040`(`nm` 实测生效)+ 恢复会挂的 export → **仍挂**,且连 `KM4 BOOT REASON` 都没出。
   ⇒ 挂的**不是 `app_start` 本体**,是它调用的某个 **callee**(在 fwlib、随 export 一起挪位),
   甚至可能是 `BOOT REASON` 之前的早期 callee(`irq_table_init`/`Cache_Enable`/`SystemSetCpuClk` 一段)。

`ld --cref` map 进一步确认:挂/通两版**拉进的归档成员、newlib 对象完全相同,无符号解析翻转**
(wolfCrypt 的 ecc/asn/sp 对象本来就被同镜像里的 wolfCrypt benchmark 拉进来了,两版都有)。

### 排除项(逐一用 bleak 确定性验证)

| 嫌疑 | 验证方法 | 结论 |
|---|---|---|
| PSRAM(本片无 PSRAM) | `app_init_psram`/`app_psram_load` 强制跳过 + 注释掉 `app_mpu_nocache_init` 里 0x02000000 的 PSRAM MPU 区 | 仍挂,排除 |
| 执行 ECDH(不是链接) | 关掉 boot 时的 KAT 调用、wolfCrypt ECC 仅链入 | 仍挂 ⇒ 是**链接**不是执行 |
| 镜像大小 | 末尾 pad 到同尺寸 | boot ⇒ 排除 |
| 符号解析翻转 / 新 newlib | `ld --cref` diff | 无差异,排除 |
| `.init_array` 构造器 | map 查 `__init_array_start==__init_array_end` | 空,排除 |
| RAM 布局/MPU | 边界符号 hang-vs-boot 逐个对比 | 字节级相同,排除 |
| `platform_stdlib_rtl8721d.h` 的宏重定向(snprintf→_rtl_snprintf "NULL function" 等) | `#define STD_PRINTF` + 注释整个头 | 仍挂,排除 |

---

## 3. 修复

**让 boot 路径之前不要多出那段代码即可。** 本例:NimBLE SM 的 wolfCrypt glue 里,公钥导出走
`mp_to_unsigned_bin_len`(unsigned-bin,不引 `wc_export_int`/`sp_tohex`),而非 `wc_ecc_export_public_raw`。
通用原则:**ECC 导出/任何路径都别经 `wc_export_int`**(它带 hex/dec stringify 分支)。

```c
/* 不要: rc = wc_ecc_export_public_raw(key, pub, &xl, pub+32, &yl);  // 拉 wc_export_int->sp_tohex */
rc = mp_to_unsigned_bin_len(key->pubkey.x, pub,      32);   /* ecc_point.x 是 mp_int[1](无 ALT_ECC_SIZE) */
rc = mp_to_unsigned_bin_len(key->pubkey.y, pub + 32, 32);
```

**注意这是"绕过"而非"根治"**:潜伏故障还在。以后若在 `app_start` 之前再加 ~1KB 代码(例如
NimBLE BLE Mesh 拉更多 wolfCrypt/host 代码),`app_start` 的 callee 可能又被顶到坏位置而复发。

### 3.1 探索过但**未采用**的备选修法(2026-06-07)

> **采用的方案仍是 §3 的 `mp_to_unsigned_bin_len`。** 下面这些都试过/验证过,但最终**回退、不采用**
> ——记录于此供后人参考。当前代码:`wolfcrypt_sm_glue.c` 用 `mp_to_unsigned_bin_len`,
> `user_settings.h`/`wolfmath.c` 无任何 gate 改动。

**(a) gate 掉死 hex 分支(验证可 boot,但未采用——需改 wolfssl submodule)**:`wc_ecc_export_public_raw`
拉进 sp_tohex/sp_radix_size,是因为 `wc_export_int`(wolfmath.c)有个 `#ifdef WC_MP_TO_RADIX` 的 hex
分支(`WC_TYPE_HEX_STR` → `mp_radix_size`/`mp_tohex`);我们只走 `WC_TYPE_UNSIGNED_BIN`,那段是死代码。
`WC_MP_TO_RADIX` 被 `types.h` 在 `HAVE_ECC && HAVE_ECC_KEY_EXPORT` 时强制打开(config 关不掉)。
做法:自定义开关 `AMEBAD_NO_MP_HEX_EXPORT` + 把 wolfmath.c guard 改成
`#if defined(WC_MP_TO_RADIX) && !defined(AMEBAD_NO_MP_HEX_EXPORT)` → sp_tohex/sp_radix_size 被
`--gc-sections` 丢弃 → KM4 正常 boot 且 `wc_ecc_export_public_raw` 可用(实测 BOOT,`ble_sync=2`)。
**未采用原因**:要 patch wolfssl submodule(维护负担);§3 的 `mp_to_unsigned_bin_len` 同样有效且只动我们自己的 glue。

**(b) 失败的尝试**:① 把 sp_tohex 等用 ld 排到 `.text` 头部凑"均匀平移"→ 仍崩(app_start 落 0x0e062fbc,
非干净均匀);② 把它们放进 `.ram_image2.text`(RAM)→ boot 时 HardFault(破坏 image2 RAM 拷贝/布局);
③ 换 -O2 / 去 `-fno-jump-tables` 向 SDK codegen 对齐 → 仍崩(只是又一次布局轮盘赌)。**注意**:潜伏的
XIP 取指 HW 敏感性始终在,以上"绕过"都只是不再触发它,治本需 Realtek(见本文 §5 / `amebad_xip_boot_hang_realtek_report.md`)。

---

## 4. 排查方法论(避免重蹈)

- **KM4 早期 LOGUART 输出不可信**:KM0 与 KM4 抢占同一 LOGUART,boot 早期(尤其 `app_start` 进 `main`
  之前)的 `DiagPrintf`/`DBG_8195A`/`DBG_PRINTF` 标记**非确定性**地丢/截断/不同 build 不同表现——
  花了十几轮在 marker 上全是噪声。`DBG_8195A` 在 `Cache_Enable` 之前还会直接挂。
- **用 bleak 当确定性信号**:设备启动成功就会广播 `amebad-nimble`;Windows 宿主
  `powershell.exe -Command "python -c 'import asyncio; from bleak import BleakScanner; …'"` 扫到 =
  boot 成功,扫不到 = 挂。这是判断"这次改动 boot 起没起"的唯一可靠手段(参见
  `project/amebad/nimble_ble_verify.py`)。
- **拿带 object 归属的真 ld map**:构建默认的 `target_img2.map` 是 stage-2 image tool 另写的 **nm 风格**
  (只有符号无归属);要做归属/解析分析,临时给链接加 `-Wl,--cref -Wl,-Map=/tmp/x.map`(改
  `app_km4/CMakeLists.txt` 的 `target_link_options`),拿到标准 GNU ld map(Archive member included +
  Cross Reference Table)。
- **二分定位**:把可疑 shim 函数逐个改成空壳(`return fail;` 不引用目标符号),配 bleak,
  快速夹出"链入哪个函数会崩"。

---

## 5. SWD 定位结果(2026-06-07,已接 CMSIS-DAP)

用 [[reference_amebad_openocd_debug]] 的 OpenOCD + RV CMSIS-DAP 把挂死的 KM4 halt 住,把 §1 的"未结"
推进到了取指层面。**复现**:`uECC_make_key` 改回 `wc_ecc_export_public_raw`(拉 sp_tohex ~1KB),
`app_start` 0x0e062c18→**0x0e062fc4(+0x3AC)**,image2 大小不变(再证位置非大小)。

**故障签名(确定性,多次一致)**:
- 落点 PC 死循环在 ROM `0x10104468`(=`BOOT_ROM_ResetVsr`,见 [[reference_amebad_chplan_reboot_handshake]]),
  LR=0xffffffb9(EXC_RETURN);**SFSR(0xE000EDE4)=0x48 = SecureFault AUVIOL + SFARVALID**,
  **SFAR(0xE000EDE8)=0x37ffe537(野地址,执行跳飞)**;CFSR/HFSR=0;全局(uxCurrentNumberOfTasks 等)
  全是垃圾值 ⇒ **.bss 清零前就挂**。
- 地址断点验证:从 app_start 放出,第一个 callee 的正确目标 ROM `0x10110794` 与 VectorTableOverride 都
  **不命中** ⇒ **app_start 自己的第一条 `blx r3` 就跳飞**,到不了任何 callee。
  (与 §2 实验3"钉 app_start 后挂的是 callee"统一为:**位置相关,谁落进坏区间谁跑飞**;钉 app_start 只换"谁倒霉",治标不治本。)

**根因坐实 = KM4 XIP 取指 desync 一条 cache-line(0x20)**:本镜像 **RSIP 关**(image_tool
`AMEBAD_RSIP_ENABLE=OFF`,明文),排除加密 desync。单步 app_start 的 PC 轨迹(c4→c6→c8→**cc**→d0)
把"CPU 执行 `ELF[地址+0x20]` 内容"套进去**完全吻合**(c8 处 ELF 是 2B `ldr`,但 CPU 走了 4B=
`ELF[0xe062fe8]` 的 `str.w`);debugger 总线读 XIP 同样偏 +0x20(`mdw 0xe062fc4`=`ELF[0xe062fe4]`)。
即 **CPU 从 XIP 这段坏地址取到的指令整体偏了一条 cache-line(32B)** → 第一条间接跳转装到错位字 →
跳 0x37ffe537。**坐实 §1 怀疑的"flash cache 取指行为",量化为 32B/一行的 fetch 错位。**

**调试踩坑**:① `reset halt`(sysresetreq)会落进 `BOOT_ROM_ResetVsr` 自旋等 reset 握手
(driver-only 不置位),手动 `mww 0x480003f8 0x02000201` 补握手才会继续 boot 到 app_start
(见 [[reference_amebad_chplan_reboot_handshake]]);② OpenOCD `cortex_m vector_catch all` **抓不到**
M33 的 SecureFault(DEMCR 无该向量位);③ OpenOCD 对 XIP 的逐指令单步/寄存器读不可靠(寄存器返 PC 值、
XIP 读时有时无),可靠信号是地址断点命中 + 自由运行后的 SFSR/SFAR + 单步 PC 轨迹的步长。

**仍未根治**:还差"为什么这段 flash 物理地址取指会偏一条 cache-line"。

### 已排除的修复/假设(2026-06-07,均实测)

| 假设/修复 | 验证手段 | 结果 |
|---|---|---|
| 钉 app_start 到固定地址 | SWD:从 app_start 放出,第一个 callee(ROM 0x10110794)/VectorTableOverride 都不命中,app_start **第一条 `blx r3` 就跳飞** | ❌ 证伪(谁落进坏区间谁挂,钉它只换倒霉鬼) |
| I-cache 持旧行 | `bootloader_km4/boot_ram_hp.c` 跳前加 `Cache_Flush()`(SCB InvalidateI/D) | ❌ **无效**:invalidate 后从 flash 重填**仍 +0x20** ⇒ 不是 cache 旧行 |
| FLASH MMU 区越界 | 读 `RSIP->FLASH_MMU`(0x48000630):KM4 XIP entry1 映射 `0x0e000000–0x0e097fff`,offset 0x05fe6000(minus);app_start 两位置都在区内、离上界(0xe097fff)很远,XIP 代码最高才 0xe088xxx | ❌ 排除(未越界) |
| 换 SPIC 读模式(cache on=16B 读) | 先读 app_start **入口** CCR=0x201 ⇒ **IC/DC=0,入口 cache 本来就关**(SDK 到 app_start:416 才开),即默认就是 4B 读;故反过来在 image1 跳前 `Cache_Enable(ENABLE)` 改 16B 读 | ❌ **仍挂**,但 **SFAR 从 0x37ffe537 变 0x37ff6537**(差 1 bit)⇒ **两种读模式都误取、只是错得不同** ⇒ 与读宽度无关,是这段物理地址取出的数据本身坏 |

⇒ **软件侧杠杆全部证伪**。根因是 **Realtek SPIC/RSIP-MMU/flash IP 对某段物理地址的取指/映射行为**(cache 开关两种读模式都坏、错值不同;非 cache 旧行、非 MMU 越界、非 app_start 位置)。IP 级(Realtek confidential),软件改不动;真要根治得问 Realtek FAE。**唯一可用方案 = `mp_to_unsigned_bin_len` 绕过 + 避免 boot-critical 代码涨到坏区间**(可加构建期护栏)。

### 调试踩坑(SWD)
- `reset halt`(sysresetreq)落进 `BOOT_ROM_ResetVsr`(0x10104468)自旋等 reset 握手,driver-only 不置位,手动 `mww 0x480003f8 0x02000201` 补握手才会 boot 到 app_start(见 [[reference_amebad_chplan_reboot_handshake]])。
- OpenOCD `cortex_m vector_catch all` 抓不到 M33 SecureFault;对 RSIP/XIP 的逐指令单步与寄存器读不可靠(寄存器返 PC 值、XIP 读时有时无),可靠信号 = 地址断点命中 + 自由运行后的 SFSR/SFAR + 单步 PC 步长。

**当前结论**:`mp_to_unsigned_bin_len` 绕过仍是唯一可用方案;**钉 app_start / image1 Cache_Flush 均已实测证伪,勿再试。**
