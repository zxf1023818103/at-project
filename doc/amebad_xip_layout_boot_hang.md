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

## 5. 未结(根治需 SWD)

精确到"哪个 boot callee、落到哪个 XIP 地址、为什么那个地址执行就挂",软件手段(串口非确定 +
map 静态分析)已到顶。要根治需 **SWD/JTAG halt 挂死的 KM4 读 PC/寄存器**(板子目前只接 COM29 串口;
AmebaD 的 openocd + CMSIS-DAP + gdb 路径见 [[reference_ameba_openocd_flash]])。怀疑与 KM4 XIP
flash cache / 某段 flash 物理地址的取指行为有关(与 [[reference_amebad_boot_rodata_xip]] 同族)。
