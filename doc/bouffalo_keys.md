# Bouffalo (BL602 / BL616 / BL616CL) eFuse 安全字段清单

整理这三颗芯片 eFuse OTP 里的安全相关字段，按**用途**分四类，对照 [`doc/amebad_keys.md`](amebad_keys.md)
（RTL8721D）排版。**来源全是 bouffalo_sdk 开源头文件**（见文末），所以**本文及其依据都可公开**
（不像 `amebad_keys.md` 的依据是 Realtek NDA PDF）。

> 术语：**SF AES** = Serial-Flash AES（flash 落盘透明加解密，类比 AmebaD 的 RSIP）；
> **SBOOT** = Secure Boot；**ROTPK** = Root-of-Trust Public Key（根公钥）；
> **Sec_Eng** = 博流硬件加密引擎（AES/SHA/GMAC/TRNG）；**PKA** = 公钥加速器（RSA/ECC）；
> **TZC** = TrustZone Controller（总线级 secure/non-secure 分区）。
>
> 来源文件（每颗芯片）：`component/bouffalo_sdk/<chip>_std/include/`
> `{hardware/ef_data_reg.h, ef_ctrl.h, ef_cfg.h, hardware/tzc_sec_reg.h, sec_dbg_reg.h}`
> 共享：`bflb_lhal/include/{bflb_sec_*.h, hardware/sec_eng_reg.h}` + `bflb_lhal/src/pka/libpka_*.a`。
> `<chip>_std/src/<chip>_ef_cfg.c` 提供语义（secure-boot/AES/pk-hash 读写）。

---

## eFuse 逻辑地图（BL616 / BL616CL，256 B = 2 × 128 B 区）

```
区 0（0x00–0x7C）
0x00  ef_cfg_0           ← 安全配置字（sf_aes / sboot / dbg / 一堆功能禁用位，见下）
0x04  ef_dbg_pwd_low   ┐  调试密码 1（64-bit）
0x08  ef_dbg_pwd_high  ┘
0x0C  ef_dbg_pwd2_low  ┐  调试密码 2（64-bit）
0x10  ef_dbg_pwd2_high ┘
0x14  …（wifi mac / trim / device info，非密钥）
0x1C  key_slot_0  (W0..W3, 128-bit)   ← 默认放 ROTPK hash 的低 128 位
0x2C  key_slot_1  (128-bit)            ← 256-bit PK hash 的高 128 位
0x3C  key_slot_2  (128-bit)
0x4C  key_slot_3  (128-bit)
0x5C  ef_sw_usage_0..3 (0x5C/0x60/0x64/0x68)   普通软件 OTP（sw_usage_0 含部分 boot 配置）
0x6C  key_slot_11 (128-bit)
0x7C  ef_data_0_lock     ← 区 0 的写/读锁位（见"保护位"）
区 1（0x80–0xFC）
0x80  key_slot_4  (128-bit)
0x90  key_slot_5
0xA0  key_slot_6
0xB0  key_slot_7
0xC0  key_slot_8
0xD0  key_slot_9
0xE0  key_slot_10 (128-bit)
0xFC  ef_data_1_lock     ← 区 1 的写/读锁位
```

**`ef_cfg_0`（0x00）安全位（BL616/CL）**：

| bit | 名称 | 含义 |
|---|---|---|
| [1:0] | `sf_aes_mode` | flash AES：0=off / 1=AES128 / 2=AES192 / 3=AES256 |
| [3] | `cpu0_dis` | 禁 CPU0 |
| [5:4] | `sboot_en` | Secure Boot 使能 |
| [7] | `sboot_sign_mode` | 签名算法选择（RSA / ECC） |
| [9:6] | `uart_dis` | 禁 UART 下载口 |
| [13:12] | `sf_key_re_sel` | flash AES 用**哪个** key slot |
| [17] | `0_key_enc_en` | key_slot_0 自身加密存储 |
| [21] | `cpu_rst_dbg_dis` | 禁复位调试 |
| [22] | `se_dbg_dis` | 禁 **Sec_Eng** 调试 |
| [23] | `efuse_dbg_dis` | 禁 eFuse 调试读 |
| [25:24] | `dbg_jtag_1_dis` | 禁 JTAG1 |
| [27:26] | `dbg_jtag_0_dis` | 禁 JTAG0 |
| [31:28] | `dbg_mode` | 调试模式（open / 密码门控 / close） |

其余功能禁用位：`sdu_dis`[14]、`btdm_dis`[15]、`wifi_dis`[16]、`cam_dis`[18]、`m154_dis`[19]、`ai_dis`[2]。

**BL602 的 `ef_cfg_0` 布局不同**（更简单）：`sf_aes_mode`[1:0]、**`sboot_sign_mode`[3:2]**、`sboot_en`[5:4]、
`dbg_jtag_0_dis`[27:26]、`dbg_mode`[31:28]。无 `se_dbg_dis`/`efuse_dbg_dis`/`sf_key_re_sel` 等。

---

## 一、Flash 落盘加密（SF AES）—— 类比 AmebaD RSIP

| 项 | 值 | 说明 |
|---|---|---|
| 算法 | **AES-128/192/256**，可选 **XTS** 模式 | `bflb_efuse_enable_aes(aes_type, xts_mode)`：写 `ef_cfg_0` 低位（aes_type）+ bit2（xts） |
| key 来源 | 某个 **key slot**（`sf_key_re_sel`[13:12] 选） | 软件写进 slot → 读锁 → 之后 CPU 读不到，只有 Sec_Eng 引擎能用 |
| 解密路径 | XIP 取指时 Sec_Eng AES 透明解密 | `se_aes_*_key_sel`（`sec_eng_reg.h` @0x140）选 key 源 = eFuse slot（硬件 key ladder） |
| key 自身加密 | `0_key_enc_en`[17] | slot_0 的内容再被一层 key 包住存储 |

- **XTS 模式比 ECB/CTR 强**：每个 flash 块用不同 tweak，防"同明文同密文"。这点比 AmebaD RSIP（单一 OTF 模式）更灵活。
- 同 RSIP 的注意点：SF AES 加密的是 **flash 内容（镜像/数据落盘）**，机密性强弱取决于把哪些区配成加密区 +
  key 是否读锁。要"运行时数据落盘机密性"，仍是看应用怎么用，硬件只给透明加解密的能力。
- **硬件 key ladder**：key 写进 eFuse slot 后置 `rd_lock`，CPU 永远读不到该 slot；但 Sec_Eng AES/GMAC 的
  `key_sel` 能把它当 key 源直接喂引擎 → 真正的"软件不可见根密钥"，等价于 AmebaD 把 RSIP/RDP key 放 secure eFuse。

## 二、Secure Boot（镜像真伪）—— 类比 AmebaD SBOOT

| 项 | 值 | 说明 |
|---|---|---|
| 签名算法 | **RSA 或 ECC**（`EF_CTRL_SIGN_RSA=1 / _ECC=2`） | `sboot_sign_mode` 选；`bflb_efuse_read_secure_boot(&sign,&aes)` 读 |
| 使能 | `sboot_en`[5:4] | BootROM 据此决定是否校验 |
| 芯片存什么 | **ROTPK 的 SHA-256 哈希**（不是公钥本体！） | 256-bit hash 放 **key_slot_0(+slot_1)**；`bflb_efuse_write_lock_pk_hash(len)`：256→锁 slot0+slot1，128→只锁 slot0 |
| 私钥 | **不在芯片上** | 厂家保管；`bflb_image_tool` 生成/用，给镜像签名 |

- **与 AmebaD 的关键差异**：AmebaD eFuse 存的是 **Ed25519 公钥本体**（32B@0x1A0）；Bouffalo 存的是
  **公钥的哈希**（ROTPK hash）。镜像头带完整公钥，BootROM 先算 hash 比对 eFuse，再用公钥验签 → 省 eFuse 空间、换算法更灵活。
- 流程同典型 secure boot：① 厂家私钥签名（image tool）→ ② 量产烧 ROTPK hash + 置 `sboot_en` + 写锁 →
  ③ 上电 BootROM 校验，失败拒绝启动。

## 三、调试保护 —— 类比 AmebaD SWD PWD

| 项 | 位置 | 说明 |
|---|---|---|
| 调试模式 | `ef_cfg_0` `dbg_mode`[31:28] | open / 密码门控 / close（4-bit） |
| 调试密码 ×2 | `ef_dbg_pwd`(0x04–0x08, 64-bit) + `ef_dbg_pwd2`(0x0C–0x10, 64-bit) | 挑战应答解锁；`bflb_efuse_rw_lock_dbg_key(rd,wr)` |
| JTAG 禁用 | `dbg_jtag_0_dis` / `dbg_jtag_1_dis` | 直接关 JTAG 口 |
| 子系统调试禁用 | `se_dbg_dis`（Sec_Eng）/ `efuse_dbg_dis`（eFuse）/ `cpu_rst_dbg_dis` | 细粒度关掉对敏感子系统的调试可见性 |
| **sec_dbg 外设** | `sec_dbg_reg.h`：`sd_chip_id` + `sd_dbg_pwd/pwd2` + `sd_status` | **仅 BL616 / BL616CL**：硬件挑战应答（chip-id 绑定），BL602 只有 eFuse 那套 |

## 四、密钥槽 / 应用密钥

| 项 | BL616 / BL616CL | BL602 |
|---|---|---|
| key slot 数 | **12** 个（slot 0–11） | **6** 个（slot 0–5） |
| 每槽宽度 | 128-bit（16 B，W0..W3） | 128-bit |
| 用途（多用途） | ROTPK hash / SF-AES key / 用户密钥 / MAC | 同 |
| 锁 | 每槽独立 `wr_lock` + `rd_lock` | 同 |
| 软件 OTP | `sw_usage_0..3` | `sw_usage_0` |

- **多用途**：同一组 key slot，既能放 secure-boot 的 ROTPK hash，也能放 SF-AES key，也能放用户/会话根密钥。
- **读锁 = 真秘密**：置 `rd_lock` 后 CPU 永久读不到该 slot，但 **Sec_Eng/PKA 引擎仍能用**（`key_sel` 取 eFuse 源）
  → 这是 Bouffalo 的"硬件信任根"机制，对应 AmebaD 的 secure-eFuse HUK（TrustZone-only）。
- 硬件消费方：**Sec_Eng**（AES/SHA/GMAC/TRNG，`sec_eng_reg.h`）+ **PKA**（RSA/ECC，`libpka_<chip>.a` + `bflb_sec_ecdsa/dsa.h`）。

---

## eFuse 保护位（lock words）

- **BL602** — 单个 `ef_data_0_lock` @0x7C：每个 slot 0–5 都有 **`wr_lock` + `rd_lock`**；`dbg_pwd` 有 wr+rd lock；
  `boot_mode` / `wifi_mac` / `sw_usage_0` 有 wr_lock。（slot_4/5 的写锁拆 `_l`/`_h` 两位）
- **BL616 / BL616CL** — 两个锁字：
  - `ef_data_0_lock` @0x7C（区 0）：`boot_mode`[14]、`dbg_pwd`[15]、`wifi_mac`[16]、`key_slot_0..3`[17:20]、`sw_usage_0/1`[21:22]…（+ 对应 rd_lock 在高位）
  - `ef_data_1_lock` @0xFC（区 1）：`wr_lock_key_slot_4..10`[15:21] + `rd_lock_key_slot_4..10`[25:31]

> **要点**：每个 key slot 都能**单独**设"写一次"（wr_lock）和"读禁止"（rd_lock）。写锁防篡改，读锁让该 slot
> 成为软件不可见的硬件密钥。这套"逐槽 rd/wr lock"比 AmebaD 的"按区固定的 RD/WR_Forbidden 位"更细。

---

## 三颗芯片差异速查

| | BL602 | BL616 | BL616CL |
|---|---|---|---|
| 内核 | RV32（单核，老） | E907 RISC-V（M/U + PMP） | E907（BL616 变体） |
| eFuse | 128 B，6 key slot | 256 B，12 key slot | 同 BL616 |
| `ef_cfg_0` | sign_mode[3:2]，位少 | 全套 *_dis + sf_key_re_sel + 0_key_enc_en | 同 BL616 |
| sec_dbg 外设 | ❌（只 eFuse dbg） | ✅（chip-id 挑战） | ✅ |
| TZC | `tzc_sec` only | `tzc_sec` + `tzc_nsec`（双视图） | `tzc_sec` |
| Sec_Eng / PKA | ✅ / `libpka_bl602.a` | ✅ / `libpka_bl616.a` | ✅ / `libpka_bl616cl.a` |

> BL616CL 的 `hardware/` 里没单独 `ef_data_reg.h`，但 `bl616cl_ef_cfg.h` API 与 BL616 一致、沿用 BL616 的 eFuse 布局。

---

## 对照 AmebaD（RTL8721D）

| 能力 | AmebaD（[amebad_keys.md](amebad_keys.md)） | Bouffalo（本文） |
|---|---|---|
| Flash 加密 | **RSIP** AES-128 OTF（key@eFuse 0x190）+ RDP AES-128-ECB | **SF AES** 128/192/256 **+ XTS**（key@某 key_slot，`sf_key_re_sel` 选） |
| Secure boot | **Ed25519**，存**公钥本体**@0x1A0(32B) | **RSA / ECC**，存 **ROTPK 的 SHA-256 哈希**@key_slot_0(/1) |
| 根密钥（HUK 等价） | secure eFuse 0x150(32B)，TrustZone-only 读 | 任一 **rd-locked key slot**（软件读不到，Sec_Eng/PKA 硬件可用） |
| 调试锁 | SWD PWD@0x180(16B) + 保护位 | `dbg_pwd`/`dbg_pwd2` 2×64-bit + `dbg_mode` + (BL616/CL) sec_dbg 挑战 |
| 硬件加密 | ROM mbedtls **软件**（HW IPSEC 引擎调不通，见 reference 记忆） | **Sec_Eng**（AES/SHA/GMAC/TRNG）+ **PKA**（RSA/ECC）硬件可用 |
| 总线/特权隔离 | TrustZone-M（KM4 Cortex-M33 SAU/IDAU） | **TZC**（总线 region 级 secure/non-secure；BL616 双视图） |
| 安全存储 | TF-M PSA ITS/PS（本项目已移植，见 [amebad_tfm_storage.md](amebad_tfm_storage.md)） | 无 TF-M；可用 key slots + Sec_Eng 自建 |
| 密钥保管 | 公钥/设备本地密钥在片；签名私钥在厂家 HSM | 同原则：芯片放 ROTPK hash / 本地密钥；签名私钥在厂家 |

---

## PSA 支持

"PSA" 要分三层看，Bouffalo 的支持度差别很大。来源：`platform/bouffalo_sdk/components/crypto/mbedtls/`
（vendored mbedTLS 3.x）+ `project/bouffalo/proj.conf`。

| PSA 层 | Bouffalo BL6xx | 证据 |
|---|---|---|
| **PSA Crypto API** | ✅ 完整 + 硬件加速 | `mbedtls/library/psa_crypto.c`；默认 `MBEDTLS_CONFIG_FILE=config-tls-generic.h`（开 `MBEDTLS_PSA_CRYPTO_C/_CONFIG`，指向 `config-psa.h`）；算法由 Kconfig 驱动（`Kconfig.psa.auto/.logic` → `CONFIG_PSA_WANT_ALG_*`） |
| **PSA 持久密钥（Crypto Storage / ITS）** | ⚠️ 有，但 **library 级、无隔离** | `psa_crypto_storage.c` + `psa_its_file.c`（`MBEDTLS_PSA_CRYPTO_STORAGE_C` 已开）—— 但 `psa_its_file.c` 是 **文件后端参考实现**，落 C stdio→LittleFS，密钥与 app 同地址空间，非安全世界可读 |
| **PSA Protected Storage（加密+隔离）** | ❌ | 无 |
| **PSA Firmware Framework / RoT 隔离** | ❌ | **无 TF-M**：唯一 "tfm" 命中是 mbedTLS 上游自带的 config 模板（`mbedtls_v3/configs/config-tfm.h` 等），无 SPE/NSPE 分区、无 PSA-RoT 服务 |
| **PSA Initial Attestation** | ❌ | 无 |
| **PSA Certified** | ❌ | 非 PSA Certified 芯片 |

**硬件加速接线**（`proj.conf`）：`CONFIG_MBEDTLS_{AES,BIGNUM,ECC,SHA1,SHA256,SHA512}_USE_HW=y`
→ PSA Crypto 的 AES/SHA/ECC/大数运算落到 **Sec_Eng + PKA**（见"第四类"）。

**对照 AmebaD（本项目移植了 TF-M）**：

| PSA 层 | Bouffalo BL6xx | AmebaD（本项目 TF-M，见 [amebad_tfm_storage.md](amebad_tfm_storage.md)） |
|---|---|---|
| PSA Crypto API | ✅ mbedTLS + Sec_Eng/PKA | ✅ SPE 内 mbedcrypto |
| PSA 持久密钥 | ⚠️ 文件后端（无隔离） | ✅ ITS（SPE 隔离） |
| PSA Protected Storage | ❌ | ✅ PS（HUK-AEAD 加密落盘） |
| PSA Firmware Framework / RoT 隔离 | ❌（无 TF-M） | ✅（TF-M SFN，isolation L1） |
| PSA Attestation | ❌（可加） | ✅（已编译掉，可开） |

**一句话**：Bouffalo **支持 PSA Crypto API（接口层 + 硬件加速）**，但**不支持 PSA 的"安全等级"**
——没有 TF-M 那种 RoT 隔离 / 隔离安全存储 / PSA-FF。要在 BL616 上拿到 AmebaD 那样的隔离 PSA
（ITS/PS 在安全世界），得**自己移植 TF-M**（BL616 是 E907，有 M/U 特权 + PMP + TZC，理论可行，
SDK 里无现成 port）。

---

## 安全隔离的替代方案（没有 TF-M 怎么办）

Bouffalo 没有 TF-M / PSA-FF（见 §PSA 支持），但**隔离硬件是齐的**，缺的只是"安全世界"软件实现。
来源：`bflb_lhal/include/arch/risc-v/t-head/{rv_pmp.h,rv_hart.c}` + `hardware/tzc_sec_reg.h` + §一/§四。

**手里有的隔离原语**

| 原语 | 证据 | 作用 |
|---|---|---|
| **RISC-V PMP** | `rv_pmp.{h,c}`，**8 entry**，R/W/X + TOR/NAPOT + M-mode lock 位 | CPU 访存/取指级隔离（≈ Cortex-M 的 SAU/MPU） |
| **M/U 特权分离** | `rv_hart.c` `rv_hart_switch_mode_from_M`（PRV_M=3/PRV_U=0，mret+MSTATUS_MPP） | 不可信代码降到 U-mode 跑 |
| **TZC 总线控制器** | `tzc_sec_reg.h`：ROM/OCRAM/WRAM/Flash/PSRAM 每区 ctrl+adr_mask+4 子区 + master-ID 门控 | 总线级 firewall，管 **DMA/其他 master**（PMP 管不到的） |
| **eFuse key ladder** | rd-locked key slot + Sec_Eng `key_sel`（§一/§四） | 密钥对**所有**软件不可见，只引擎能用 |
| 第二核隔离 | — | ❌ BL602/616/616CL **单核**（BL808 才多核） |

**替代方案（按逼近 TF-M 隔离强度排序）**

- **A) PMP + U-mode 自建 micro-TEE** —— 最接近 TF-M。M-mode 跑极小 secure monitor（持密钥、做 crypto、
  访问 Sec_Eng 寄存器），PMP 把 secure RAM/flash/引擎寄存器圈成"仅 M-mode + locked"；RTOS+应用降 U-mode；
  U→M 经 `ecall` 进安全服务。即把 TrustZone-M 模型搬到 RISC-V（**PMP=SAU，U-mode=NS**）。可借 **Hex Five
  MultiZone**（RISC-V PMP TEE）/ **Zephyr RISC-V userspace（PMP 后端）**。⚠️ **无官方 TF-M RISC-V port**
  （TF-M 只针对 Armv8-M）→ 要移植或自写 monitor，SDK 无现成，非小工程。
- **B) TZC 总线隔离（A 的必要补充）** —— PMP 只挡 CPU，挡不住 DMA/SDH/USB/WiFi-BT-MAC 直接读 secure 区；
  TZC 把 secure 区设 secure-only + master-ID 门控补洞。**PMP=SAU，TZC=总线 filter**，配合才完整。
- **C) eFuse key ladder（零隔离护密钥，最轻量）** —— 只想"根密钥不被软件读到"时不用代码隔离：根密钥烧
  rd-locked slot，只让 Sec_Eng/PKA 用，CPU 永读不到。代价：不隔离 crypto **中间态**（会话密钥/明文在 RAM，
  软件可读）。适合 flash 加密 / 安全启动 / 设备认证，全现成 API。
- **D) 安全启动信任链 + 应用层加密（无运行时隔离，但有完整性）** —— Secure boot（§二）保证只跑签名固件
  → 无不可信代码注入 → 单一信任域里"软件互信"；配 SF-AES（§一）防物理读盘。这是"消灭不可信代码"，
  不是隔离。**全自家固件、无第三方/无 downloadable code 时足够**。

**判断**
1. 能做隔离，**硬件不缺**（PMP 8 区 + M/U + TZC + key ladder）；缺的是安全世界软件实现（SDK 无、TF-M 无 RISC-V port）。
2. 要 **TF-M 级隔离** → 上 **A + B**（移植 MultiZone 或自写 monitor），非小工程。
3. 只要**护密钥 / 安全启动 / flash 加密** → **C + D 足够**，零隔离开销，全现成 API（多数 IoT 量产即此等级）。
4. 对本项目（AT 固件、全自家代码、无 downloadable）：实际 **C + D 已满足**；真要"跑第三方不可信代码 + PSA 隔离存储"再投 A + B。

---

## 厂家视角：谁该留、谁该焚、谁该烧

| 材料 | 在哪 | 厂家怎么处理 |
|---|---|---|
| **Secure-boot 签名私钥** | 片外（image tool） | **HSM 重点保管**，泄露=能签恶意固件 |
| ROTPK（公钥） | 片外，hash 烧 key_slot_0(/1) | 公开；芯片只存其 SHA-256 |
| **SF-AES key** | eFuse key_slot（`sf_key_re_sel` 选） | 量产权衡：每片唯一（安全）/ 产品级共享（简单）；写后置 wr+rd lock |
| 设备根密钥 | rd-locked key slot | **每片随机、烧完即焚、零留存**；只做设备本地用途 |
| 调试密码 | `dbg_pwd`/`pwd2` | 量产烧 + 视需要锁；保留量产调试通道才留 open/password |

**总原则**（与 AmebaD 一致）：芯片里放**公钥哈希 / 设备本地密钥**；厂家库只留**签名私钥**这类"能造假"的；
**绝不留设备根密钥副本**。烧 eFuse 一次性不可逆——用 `bflb_efuse_*` API / `bflb_image_tool`，写错区可能损坏芯片。

---

## 已核实来源 / 注意

- 全部字段来自 **bouffalo_sdk 开源头文件 + `<chip>_ef_cfg.c`**（见开头来源清单）→ 本文可公开。
- `read_secure_boot` 读 `ef_cfg_0` 的 `sboot_sign_mode`(BL616 @bit7 / BL602 @[3:2]) + `sf_aes_mode`[1:0]；
  `enable_aes` 写 `ef_cfg_0` 低位 + bit2(xts)；`write_lock_pk_hash` 写 `ef_data_0_lock` 锁 key_slot_0(/1) —— 据此判定
  **PK hash 存 key_slot_0(/1)**。
- BL616 头文件里 `ef_cfg_0` 的 bitfield 结构体与命名 `*_POS` 宏在个别相邻位上不完全自洽（不同 SDK 代次）；
  安全字段以 **`.c` 实际用法 + 命名 `*_POS/_MSK` 宏**为准。
- 没有 OpenTitan / Ibex / Caliptra 任何痕迹——三颗都是博流自带安全 IP，不是 RoT 协处理器宿主。

---

## BL616/BL618 Secure Boot 官方应用手册要点

来源:`doc/BL616_BL618_Secure_Boot_Application_Note_v2.0.pdf`(博流 2023,已随仓)。上面的字段清单是从
SDK 头文件反推的;这份官方 AppNote 把 BL616/618 的具体数值/流程钉死了,和上文互为印证。

### 信任链(BootROM → Bootloader → App)
每一级都:**AES CTR/XTS 解密**(密钥来自 eFuse,Flash 控制器 XIP 时透明解)+ **ECDSA 验签**。
BootROM 是 Root of Trust,跳到 Bootloader 后**自保护**(Bootloader 阶段读不到/执行不了 BootROM)。

### 签名(ECDSA,不是 amebad 的 Ed25519!)
- **算法 ECDSA,曲线 SECP256R1(P-256)**,Boot2 和 App 都签。密钥用 OpenSSL 生成:
  `openssl ecparam -name secp256r1 -genkey -noout -out privatekey_uecc.pem` + `-pubout publickey_uecc.pem`。
- 工具:`bflb_iot_tool --chipname bl616 --boot2 ... --firmware ... --pt <partition.toml> --dts ... --pk publickey_uecc.pem --sk privatekey_uecc.pem --build`。
- **签名后固件头**:偏移 **0x100 存 64 字节公钥**,偏移 **0x148 存 64 字节签名**;固件体不变(只在头部加公钥+签名)。
- **ROTPK 机制**:BootROM/Bootloader 从固件头读公钥 → 算 **SHA256(公钥)** → 和 eFuse 里烧的 **公钥 hash(ROTPK)** 比 →
  再用镜像 SHA256 验签。**eFuse 存的是公钥的 SHA256(32B),不是整把公钥。** `efusedata.bin` 偏移 **0x1C** 存这 32B。
- 对比 amebad:amebad boot ROM 用 **Ed25519**(见 `doc/amebad_keys.md`),BL616 用 **ECDSA-P256**——所以 amebad 的
  AWS-Signer(ECDSA)在 boot 验签用不了,BL616 反而天然吻合 ECDSA-P256。

### 加密(Flash AES CTR/XTS = amebad RSIP 的等价物)
- **Flash OTF 解密单元:AES CTR/XTS**,长度 128/192/256,**推荐 AES-CTR-128**;XIP 读时硬件自动解,无软件参与,
  密钥读保护后任何软件读不到。工具:`bflb_iot_tool ... --key "<32hex>" --iv "<32hex>" --build`(默认 Boot2 与 App 同 key/IV)。
- **加密后固件头**:**AES IV(16B)存在固件头偏移 0x100**,随固件写 flash;bootheader 占 flash `0x10000~0x10FFF`,实体从 `0x11000` 起。
- **加密+签名同时**:先加密再签,头部同时含公钥+签名+AES IV(IV 排在公钥/签名之后)。
- AES key 存 `efusedata.bin` 偏移 **0x3C**(AES-128=16B / AES-256=32B),可设读保护。

### eFuse 版图(权威数值)
- **4096 bits = 512 字节,Base Address `0x20056000`**;可存签名公钥 SHA256 / 镜像加密密钥 / 用户数据密钥 / 安全启动配置 / 用户自定义数据。
- 关键字段(AppNote 表 4.1):`0x1C~0x3C` = **Bootloader 公钥 hash**(ROTPK,Slot0/1);`0x1A0~0x1C0` = **App 公钥 hash**;
  `0x180~0x1A0` = **Anti-Rollback version**;`0x3C` 起 = Bootloader/APP XIP AES key、Sec Eng AES key slots(每 slot 16B);
  `0x7C/0xFC/0x1FC` 处是 eFuse 读写锁。每字段可**独立烧读写保护**(锁后不可读不可改)。
- **应用数据加解密**:Sec Eng AES 可用 eFuse 里的固定密钥(按 16B slot 组织,`key_sel` 选 slot),API
  `Sec_Eng_AES_Set_Key_IV_BE(SEC_ENG_AES_ID0, SEC_ENG_AES_KEY_HW, key, iv)`(keySrc=HW 即取 eFuse)。

### Anti-Rollback / 量产 / JTAG
- **Anti-Rollback**:Bootloader 启动时查 App 版本号 ≥ eFuse 安全版本号才启;OTA 前也先查、小于就直接放弃下载。
- **量产/烧录**:`bflb_iot_tool --chipname bl616 --port <COM> --baudrate 2000000 --addr 0x0 --firmware whole_flash_data.bin --efuse efusedata.bin --single`。
  下载模式:按住 BOOT_SEL → 按 CHIP_EN/Reset → 松 Reset → 松 BOOT_SEL。
- **Security Debug**:JTAG 可设**密码模式**(输对密码才开)或**关闭模式**(永久禁 JTAG),防第三方经 JTAG 窃取固件/数据。
