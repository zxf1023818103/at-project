# AmebaD (RTL8721D/RTL8720DN) wolfSSL — 硬件加速优化评估

加入 `component/thirdparty/wolfssl`(submodule,pin `v5.9.1-stable`,最新 stable)后,
评估在 **driver-only KM4 image** 上给 TLS 加密做"硬件加速"的可行方案。
用例:AT 固件的 MQTT-over-TLS / HTTPS(`at_cmd/atcmd_mqtt.h` 已在树内)。

**结论先行**:RTL8720DN 的 SEC/IPSEC 硬件加密引擎在本 image 里**调不通**(已实机证明,
见 [[reference_amebad_hw_crypto]]),wolfSSL 也**不带任何 Realtek/Ameba HW 端口**——所以
"硬件引擎卸载"这条路是死的。真正能拿到的加速是 wolfSSL 给 **Cortex-M33 手写的汇编**
(SP 大数 + Thumb2 对称/哈希),纯 CPU、绕开死引擎,且**快过当前 image 用的 ROM mbedtls 2.4.0
纯 C**。换言之:**"硬件加速"在这块片子上应理解为"指令级/汇编级加速",不是"独立加密引擎卸载"。**

---

## 1. 硬件加密引擎:为什么是死路(决定性约束)

- **SEC/IPSEC 引擎**:silicon block 在、ROM API(`CRYPTO_*` / `rtl_crypto_aes/sha`)在,但
  driver-only NTZ image 里 `CRYPTO_Init` 纯写"成功",**首次 process 路径寄存器读直接 hang**
  ——缺完整 SDK secure-boot 给 SEC 引擎做的总线级 bring-up(安全属性/MPC + 时钟门控);
  efuse 空白。RSIP(XIP flash OTF 解密)同理依赖 efuse OTF key,未启用。详见
  [[reference_amebad_hw_crypto]] / [[reference_amebad_rsip_iv]]。补 secure 外设 bring-up 是
  闭源 silicon 的大坑,与本固件目标无关——**不碰**。
- **wolfSSL v5.9.1 无 Realtek 端口**:`wolfcrypt/src/port/` 下有 Renesas / NXP / ST / Maxim /
  Silabs / Xilinx / Espressif … **没有 Realtek/Ameba**,全树 grep 无 `RTL872*`/`Ameba`/`IPSEC`。
  即便有,也会撞同一堵墙。
- **Cortex-M33 没有 ARMv8-A crypto 指令**:`aese/aesd/sha256h` 那套是 aarch64 专属。
  wolfSSL 的 `armv8-*.S`(HW AES/SHA 指令版)在 M33 上全程被 `__aarch64__` 守卫挡掉。
  → **指令级 AES/SHA 硬件也没有**。

**所以本 image 的加密只能是软件**;问题收敛为:**哪种软件最快**。

---

## 2. 加速的真正载体:KM4 = Cortex-M33 + DSP + FPU

本树实测编译标志(`build/km4` compile commands):

```
-march=armv8-m.main+dsp+fp  -mthumb  -mcmse
-mfloat-abi=hard  -mfpu=fpv5-sp-d16  -D__FPU_PRESENT  -DCONFIG_PLATFORM_8721D
```

- ARMv8-M Mainline,**带 DSP 扩展**(`+dsp` → `__ARM_FEATURE_DSP`,UMAAL 可用)
- **硬 FPU**(fpv5-sp-d16,单精度,hard ABI)
- 这正是 wolfSSL Cortex-M 汇编想要的底座:`UMULL/UMLAL`(SP 大数)与 Thumb2 对称/哈希汇编
  都是 ARMv4+/Thumb2 指令,**无需** ARMv8-A crypto 扩展。

---

## 3. 可落地的加速:wolfSSL 的两层 Cortex-M 汇编(都纯 CPU,绕开死引擎)

### 层 1 — SP 大数汇编:握手加速(**最大的赢面**)

TLS 握手在 MCU 上 80%+ 的 CPU 花在公钥运算(ECDHE/ECDSA P-256、RSA 2048/3072 验签)。
wolfSSL 用 **single-precision math + Cortex-M 内联汇编**专门打这块。

| 开关 | 作用 | 实现文件 |
|---|---|---|
| `WOLFSSL_SP_MATH_ALL` | 对 RSA/DH/ECC 全用 SP 大数 | `wolfcrypt/src/sp_int.c` 调度 |
| `WOLFSSL_SP_ARM_CORTEX_M_ASM` | Cortex-M 内联汇编(`UMULL/UMLAL`) | `wolfcrypt/src/sp_cortexm.c` |
| (`WOLFSSL_HAVE_SP_RSA` / `_ECC` / `_DH` 随上面自动开) | | |

- 覆盖:**ECC P-256**(默认)、RSA **2048/3072**(默认;4096 需 `WOLFSSL_SP_4096`)、DH。
  P-384/P-521 需 `WOLFSSL_SP_384/521` 显式开。
- `+dsp` 下 UMAAL 可用;wolfSSL 为兼容性在 ARMv7-M 默认 `WOLFSSL_SP_NO_UMAAL`,
  可按需放开换一点点 bignum 速度(**micro-opt,先测再说**)。
- 相对当前 image 的 **ROM mbedtls 2.4.0 mpi(纯 C 大数)**:SP+asm 通常是**数倍**握手提速。

### 层 2 — Thumb2 对称/哈希汇编:记录层 + AEAD 吞吐

| 开关 | 作用 |
|---|---|
| `WOLFSSL_ARMASM` + `WOLFSSL_ARMASM_THUMB2` | 开 Thumb2 汇编路径 |
| `WOLFSSL_ARMASM_NO_HW_CRYPTO` | 显式声明无 ARMv8-A crypto 指令(M33 必须) |

`wolfcrypt/src/port/arm/` 下 Cortex-M 适用(Thumb2)的手写汇编:
`thumb2-aes-asm.S`(AES,含 CBC/CTR/GCM 底座)、`thumb2-sha256-asm.S`、`thumb2-sha512-asm.S`、
`thumb2-sha3-asm.S`、`thumb2-chacha-asm.S`、`thumb2-poly1305-asm.S`、`thumb2-curve25519.S`
(X25519/Ed25519)。→ 覆盖 **AES-GCM / ChaCha20-Poly1305 两套主流 AEAD + SHA-2 + X25519** 全在汇编上。

> 注:`armv8-*.S` 与 `armv8-32-*.S` 是 aarch64 / 32-bit ARM ISA,M33 用不上,会被守卫挡掉——
> 选 Thumb2 路径即可,不会误编。

---

## 4. 不要做的事

- **别折腾 IPSEC/SEC 引擎**:已实机证明 hang,补 secure 外设 bring-up 是闭源大坑。
- **别用 `WOLF_CRYPTO_CB` 把对称/哈希卸载到 ROM mbedtls**:两者都是软件,而 ROM mbedtls 2.4.0
  是**纯 C 表实现(无 Thumb2 asm)**,比 wolfSSL 自己的 Thumb2 汇编**更慢**——卸载过去是**减速**。
  crypto-callback 只有在接**真硬件引擎**时才有意义,而我们没有。
  (`WOLF_CRYPTO_CB` 框架本身可留作未来对接外部引擎的口子:`wolfssl/wolfcrypt/cryptocb.h`,
  `wc_CryptoCb_RegisterDevice(devId, cb, ctx)` + `wc_CryptoInfo`。)
- **别期待指令级 AES/SHA**:M33 无 ARMv8-A crypto 扩展。

---

## 5. 前置项(非"加速"但 TLS 必需 —— 真正的风险点在这)

- **熵源 / RNG**:wolfSSL 需要 `wc_GenerateSeed()`。平台现有:
  - `rtw_get_random_bytes()`(osdep,WiFi 栈在用、能工作)——但实现是 `random_seed` 喂的
    **软件 PRNG**,**不是经过认证的 TRNG**;
  - ROM `RandBytes_Get`(0x1010a825)/ `Rand_Arc4`(0x1010a7f1)。
  → 把 `wc_GenerateSeed` 接到当前可得的最佳源即可先跑通;但**生产 TLS 的长期密钥质量取决于熵**,
  这是比"速度"更该认真对待的**安全项**,需单独定论(理想是真 TRNG)。
- **footprint**:SP 大数 + Thumb2 汇编吃 flash;AES-GCM 用 `GCM_TABLE_4BIT`(~256 B 表,
  速度/体积折中)而非 `GCM_TABLE`(~4 KB)。`SINGLE_THREADED`/`NO_FILESYSTEM`/`WOLFSSL_BARE_METAL`
  裁掉 OS/FS 依赖。

---

## 6. 建议的 `user_settings.h` 核心开关(裸机交叉编译)

走 `-DWOLFSSL_USER_SETTINGS`(根 `CMakeLists.txt` 支持 `WOLFSSL_USER_SETTINGS=ON`,
或自建一个薄 component 像其它 thirdparty 那样):

```c
/* --- 平台/OS 裁剪 --- */
#define WOLFSSL_USER_SETTINGS
#define WOLFSSL_BARE_METAL
#define SINGLE_THREADED
#define NO_FILESYSTEM
#define NO_WRITEV
#define WOLFSSL_SMALL_STACK          /* M33 栈紧 */

/* --- 握手加速:SP + Cortex-M 汇编(本评估的核心) --- */
#define WOLFSSL_SP_MATH_ALL
#define WOLFSSL_SP_ARM_CORTEX_M_ASM
#define WOLFSSL_HAVE_SP_RSA
#define WOLFSSL_HAVE_SP_ECC
/* #define WOLFSSL_SP_NO_UMAAL 默认开;+dsp 想放开 UMAAL 就反注释删它,先测 */

/* --- 记录层/AEAD 加速:Thumb2 汇编 --- */
#define WOLFSSL_ARMASM
#define WOLFSSL_ARMASM_THUMB2
#define WOLFSSL_ARMASM_NO_HW_CRYPTO   /* M33 无 ARMv8-A crypto 指令 */

/* --- 现代算法套件(都落在 Thumb2 asm 上) --- */
#define HAVE_AESGCM
#define GCM_TABLE_4BIT
#define HAVE_CHACHA
#define HAVE_POLY1305
#define HAVE_CURVE25519
#define HAVE_ED25519
#define HAVE_ECC                      /* P-256 */
#define WOLFSSL_SHA512

/* --- 熵源:接平台 RNG(质量见 §5) --- */
#define WC_RNG_SEED_CB   /* 或自定义 wc_GenerateSeed -> rtw_get_random_bytes / RandBytes_Get */
```

编译需把 `sp_cortexm.c` + `port/arm/thumb2-*.S`(及其 `_c.c` 包装)纳入源集;
`.S` 用 `user_settings_asm.h`(`WOLFSSL_USER_SETTINGS_ASM`)。

---

## 7. 实测结果(KM4 Cortex-M33 @200MHz,wolfSSL v5.9.1,-Os)

已落地并实机跑通:`component/amebad/wolfssl/`(wolfCrypt lib + `user_settings.h`)+
`app_km4/wolfcrypt_bench.c`(SysTick 周期计时——**DWT CYCCNT 在本 NTZ image 读 0,
和 HW crypto 引擎一样被 secure-gate**,故用 SysTick 派生 CPU 周期,实测基频 200.000 MHz)。
三档构建(CMake 选项 `AMEBAD_WOLFSSL_SP_ASM` / `AMEBAD_WOLFSSL_ARMASM`):
A=SP汇编+对称C、B=SP汇编+Thumb2对称汇编、C=全 portable C。

**对称/哈希吞吐(KB/s,越大越好):**

| 算法 | C 基线(C) | Thumb2 asm(B) | 提速 |
|---|---|---|---|
| AES-128-GCM        | 338  | **2083** | **6.0×** |
| SHA-256            | 3352 | 4160 | 1.25× |
| SHA-512            | 1660 | 2072 | 1.25× |
| ChaCha20-Poly1305  | 5179 | 6132 | 1.18× |

→ **AES-GCM 是最大赢家**:M33 无 AES 指令,portable C 的 AES+GHASH 惨(338 KB/s);
Thumb2 汇编一把拉到 2 MB/s。ChaCha20-Poly1305 即便纯 C 也已是最快的 AEAD(5 MB/s),
适合做 M33 上的首选 cipher suite。

**公钥/握手(单次耗时,越小越好):**

| 操作 | C 基线(C) | SP asm(A/B) | 提速 |
|---|---|---|---|
| ECDSA P-256 keygen | 64.6 ms | **5.2 ms**  | **12.4×** |
| ECDSA P-256 sign   | 71.5 ms | 7.7 ms      | 9.3× |
| ECDSA P-256 verify | 130.1 ms| 15.0 ms     | 8.7× |
| ECDHE P-256 agree  | 64.6 ms | **10.2 ms** | 6.3× |
| RSA-2048 verify    | 12.5 ms | 7.3 ms      | 1.7× |
| RSA-2048 sign      | 702 ms  | 269 ms      | 2.6× |
| X25519 keygen/agree| 8.9 ms  | **4.5 ms**(Thumb2)| 2.0× |

注:X25519 不走 SP 系统,提速来自 Thumb2 的 `curve25519` 汇编(仅 Build B),故 8.9→4.5 ms。
RSA-2048 用了 `WOLFSSL_SP_SMALL`(紧凑但偏慢的汇编);要更快可去掉 SP_SMALL 换 flash。

**结论(实测验证了评估的两条主张)**:
- 一次 ECDHE-P256 + RSA-2048-verify 的握手非对称开销 ≈ **10.2+7.3 = 17.5 ms**(asm)
  vs **77 ms**(纯 C)——**SP Cortex-M 汇编把 MCU 上的 TLS 握手从"勉强"变"实用"**。
- 记录层 **AES-GCM 6× / X25519 2×**——Thumb2 汇编值回票价。
- 全程**无硬件引擎**,纯 CPU,绕开死掉的 SEC 引擎。flash 代价:wolfCrypt 给 image2 加约
  +100 KB text(487→590 KB),仍宽松落在 OTA1 区(~1.5 MB)。

**对照 image 现状的 ROM mbedtls 2.4.0**:它是 portable 表 C(无汇编),对称吞吐约等于上表 C 基线档
(AES 无 GHASH 会略好,但 GCM 整体仍受限),且 ROM 里**没有可用的 ECC/RSA 子集**——
所以握手只能靠 wolfSSL。即"要 TLS 就上 wolfSSL+汇编",ROM mbedtls 只够做零散对称/哈希。

复现:`cmake build/km4 -DAMEBAD_WOLFSSL_SP_ASM=<ON|OFF> -DAMEBAD_WOLFSSL_ARMASM=<ON|OFF>` →
`build_image_all.py` → flash → 看 `[WCB]` 行(banner 自带当前档位标签)。

---

## 8. 推荐

- **采用 wolfSSL,纯软件 + Cortex-M33 汇编(SP `sp_cortexm.c` + Thumb2 `port/arm/thumb2-*`)**;
  **不接任何硬件引擎**(死路,已证)。这套既绕开 SEC 引擎的 bring-up 坑,又比 image 现有的
  ROM mbedtls 2.4.0 纯 C 快。
- 把 **熵源**当独立安全项定论(§5),别和"速度"混谈。
- 落地顺序建议:先薄 component 接进 KM4 build + `user_settings.h` → 跑 benchmark 取实数(§7)
  → 再决定 cipher suite 取舍与 footprint 调参。

## 关键事实出处
- HW 引擎死因:[[reference_amebad_hw_crypto]];RSIP:[[reference_amebad_rsip_iv]]
- KM4 编译标志:`build/km4` compile commands(`-march=armv8-m.main+dsp+fp`,`fpv5-sp-d16`)
- wolfSSL 端能力:`component/thirdparty/wolfssl/wolfcrypt/src/sp_cortexm.c`、
  `wolfcrypt/src/port/arm/thumb2-*.S`、`wolfssl/wolfcrypt/cryptocb.h`、根 `CMakeLists.txt`
- 熵源:`platform/ameba-rtos-d/component/os/os_dep/osdep_service.c:1065`(`rtw_get_random_bytes`)、
  ROM `RandBytes_Get`/`Rand_Arc4`(`app_km4/rlx8721d_rom_symbol_acut.ld`)
