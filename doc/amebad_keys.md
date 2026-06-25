# AmebaD (RTL8721D) 密钥 / 证书清单

整理这颗芯片涉及的所有密钥/密钥材料,按**用途**分四类。大多数在片内 **eFuse OTP**(一次性、可读锁),
少数是软件运行时派生、或由厂家在片外保管。来源:Realtek NDA 应用文档(`AN0411 Secure eFuse`、
`AN0410 Secure Boot`、`AN0405 RSIP`)放在 [`doc/realtek_an/`](realtek_an/README.md)(**NDA,已 gitignore,
不提交**),加上 SDK `component/amebad/fwlib/rtl8721d_efuse.h` + bootloader
`component/amebad/bootloader_km4/boot_flash_hp.c`。本文是从那些文档**提取核实后的事实**,可公开。

> 术语:**RSIP** = Realtek Secure Image Protection(flash OTF 透明加密);**RDP** = Read Data
> Protection(对镜像区做 AES 解密);**HUK** = Hardware Unique Key(设备根密钥);**ROTPK** = Root
> of Trust Public Key;**SBOOT** = Secure Boot。

---

## eFuse 逻辑地图（密钥相关区）

```
0x000~0x11F  系统 autoload / WiFi 校准 / HCI         （非密钥）
0x130        User0 (16B, Software R/W, NS 可读)       一般用户 OTP —— 别放秘密
0x140        User1 (16B, Software R/W, NS 可读)       一般用户 OTP —— 别放秘密
───────────  EFUSE_SECURE_START = 0x150 (TrustZone-secure, NS 读不到) ───────────
0x150~0x16F  User2+User3 (32B)  ← 用户可用 secure eFuse  =  HUK
0x170~0x17F  RDP KEY (16B)
0x180~0x18F  SWD PWD (16B)
0x190~0x19F  RSIP KEY (16B)
0x1A0~0x1BF  SBOOT PK (32B)  = Ed25519 公钥 (PK_L16 + PK_H16)
0x1C0~0x1C1  Protection Configuration (2B)            保护位（见下）
0x1D0        ADC Calibration                          （非密钥）
物理视图: 0x80~0x9F 32B user OTP(NS 可读) | 0xA0 OTF KEY(16B,user 不可读) |
          0xB0 RDP KEY(16B,user 不可读) | 0xC0 RDP EN
```

---

## 一、Flash 落盘加密（防"拆芯片物理读"）

| 名称 | eFuse | 大小 | 算法 | 作用 | 谁知道 |
|---|---|---|---|---|---|
| **RSIP KEY** | `0x190`（物理 OTF KEY `0xA0`） | 16B | **AES-128**（OTF，模式 Realtek 不公开） | 给 flash **镜像**(代码)加密;CPU/Cache XIP 读时硬件透明解密 | 设备;读/写锁(`RSIP_KEY_RD/WR_Forbidden`) |
| **RSIP IV** | 不在 eFuse | 16B | — | RSIP 加密的 IV,build/imagetool 用（Realtek 私发,**⚠️NDA**,见下） | Realtek / 厂家 build |
| **RDP KEY** | `0x170`（物理 `0xB0`） | 16B | **AES-128-ECB** | bootloader 用它解密受 RDP 保护的镜像(IMG3 secure + NSC) | 设备;TrustZone,硬件用 |

**RSIP（AN0405 已核实)关键点**:
- RSIP 加密的是**镜像**(build 时用 RSIP Key + IV 把 km0/km4 image 加密);运行时 CPU 在 SPIC auto_mode
  读 flash 走 OTF 硬件解密。Key **16 字节(AES-128)**,IV 必须找 Realtek(机密)。
- **⚠️ 运行时写 flash 不会自动加密**(AN0405 §1.3):CPU 写进去的是明文,但 RSIP 开着时读回会被"解密"→ 乱码。
  所以运行时要写的区(LittleFS / ITS/PS 数据区)**必须设成 No-RSIP mask 区**(`rtl8721d_bootcfg.c` 配,默认
  `0x08003000~0x08003FFF` 已 mask)。**结论:这些 No-RSIP 区里的数据是明文落盘,RSIP 不保护它们。**
- 所以 **RSIP 保护的是"代码/镜像机密性(防拷贝/防克隆)",不是"运行时数据落盘机密性"**。想让 LittleFS 里
  存的**数据**也加密落盘,RSIP 给不了,得软件加密(PS 的 HUK-AEAD,或 app 自己加密后再存)。
- RSIP Enable = 逻辑 map `0xE[2]`;另有 RSIP Key 读/写保护位(写完 key 读回校验后再开读写锁防泄露/篡改)。
- RDP 与 RSIP 可叠加:"若 RDP+RSIP,IMG3 只需 RDP 加密"。
- RSIP/RDP 都**不防"NS 软件读"**(CPU 读到的是已解密明文)。

> **⚠️ NDA — RSIP IV 实际值(Realtek FAE 私发,confidential)。** ImageTool 文档红框明确写
> "User should consult Realtek for RSIP IV, because it is confidential"。本仓是 **private repo** 故此处可记;
> **转 public / 外发前务必删掉这一段**。前 12 字节由 FAE 提供,后 4 字节补 `0x00` 到 16 字节:
>
> ```
> IV[0..11] = 90 ac d3 4e c7 0a 53 fa 6e ba 8c a4   (+ 00 00 00 00 → 16B)
> 32-hex   = 90acd34ec70a53fa6eba8ca400000000
> ```
>
> 落地:`component/amebad/image_tool/CMakeLists.txt` 的 `AMEBAD_RSIP_IV` 默认值即此(仅 `-DAMEBAD_RSIP_ENABLE=ON`
> 时生效)。**KEY 不是 Realtek 给的**——红框只对 IV 标 confidential;`AMEBAD_RSIP_KEY` 是用户自定 128-bit,
> 本地 `openssl rand -hex 16` 拍一个即可。Realtek 这套加密模型:IV 全局固定(ROM 只认这一个 nonce),KEY 用户私有。

## 二、Secure Boot（镜像真伪 / 完整性）

| 名称 | 位置 | 大小 | 算法 | 作用 | 谁持有 |
|---|---|---|---|---|---|
| **SBOOT 公钥** | eFuse `0x1A0~0x1BF` | 32B | **Ed25519** | 烧的是 **Ed25519 公钥本体**(32B,不是哈希);ROM 用它 `ed25519_verify_signature` 验镜像签名 | 公开,可烧/可读 |
| **SBOOT 签名私钥** | **不在芯片上** | — | Ed25519 | 给 bootloader/镜像签名 | **厂家私钥库(HSM)** |
| SECURE BOOT EN | eFuse `0x1C0` bit13 | 1bit | — | 使能 secure-boot 校验 | — |

- 实测路径(`boot_flash_hp.c:571-575`):从 `0x1A0` 读 32B 公钥 → `ed25519_verify_signature(sb_sig, image, len, pk)`。
  Ed25519 公钥正好 32B,所以 eFuse 存的是**公钥本体**(AN0410 给的样例 key 也是 64 hex=32B)。
- **流程(AN0410 三方)**:① Customer 生成 ECC key-pair、给镜像签名(ImageTool 把签名信息追加进 image header);
  ② Module maker 烧 flash + eFuse(烧公钥 + 置 enable 位);③ End-user 上电,**ROM** 校验签名 → 过则继续、
  **不过则死循环**。签名计算的起址/长度固定(不可配),覆盖 image2 数据。
- **enable / 保护位**(物理 map,与 AN0411 逻辑 `0x1C0` 一致):`Secure Boot Enable` = `0x1C1[5]`
  (=逻辑 `0x1C0` bit13;**0=使能**,反逻辑)、`Secure Img2 Enable` = `0x1C1[7]`(image2 也签名校验)、
  `SBOOT_PK_WR_Forbidden` = `0x1C0[6]`(公钥写保护)。烧录流程:写公钥到 eFuse → 读回校验 → 开写保护。
  (两份文档 AN0410 / `Secure Boot and Image2_v02` 内容一致。)
- **私钥**:ImageTool 生成在 `key_pair.txt`,AN0410 原文"**You should preserve private key properly and
  should not disclose to others**"——这是最该重点保护的材料(泄露=能签恶意固件)。
- 非对称:芯片存**公钥**(可公开),厂家保管**私钥**。

## 三、调试保护

| 名称 | eFuse | 大小 | 作用 |
|---|---|---|---|
| **SWD PWD** | `0x180` | 16B | JTAG/SWD 调试密码 |
| SWD 保护位 | `0x1C0` bit 0/3/8–12 | — | `SWD_PWD_RD/WR_Forbidden`、`SWD_Protection_Enable`、Secure/NS × Invasive/Noninvasive Debug Disable |
| DEBUG 保护 | `0x1C0` bit14 | — | `CPU_PC_DBGEN`(禁止从调试口读 KM4/KM0 PC) |

## 四、应用 / 安全存储（软件层,根在 eFuse）

| 名称 | 位置 | 算法 | 作用 | 谁知道 |
|---|---|---|---|---|
| **HUK**（设备根密钥） | eFuse `0x150~0x16F`(32B,TZ-secure,NS 读不到) | — | TF-M PS / 本地密钥派生的根 | **每片唯一、烧完即焚、厂家零留存** |
| **PS 存储密钥** | 不落盘,运行时派生 | HKDF-SHA256(HUK) | PS AEAD(CCM)加密 | 派生,不存 |
| **Crypto NV seed** | 存 ITS(flash `0x3F0000`) | — | mbedcrypto DRBG 熵种子 | 设备本地 |
| **IAK**(attestation 私钥) | 当前 attestation 关 | ECDSA/EC | PSA 远程证明签名 | 私钥设备留、公钥厂家留 |
| ROTPK(TF-M 侧) | 本端 BL2 OFF,镜像校验交给 Ameba secure boot(见第二类) | — | — | — |

HUK 的接入见 [amebad_tfm_storage.md](amebad_tfm_storage.md) §8(`crypto_keys.c` 读 `0x150`)。

---

## eFuse 保护配置位（`0x1C0`, 2B = Table 2-2 摘要）

| bit | 名称 | 0 = |
|---|---|---|
| 0 | SWD_PWD_RD_Forbidden_EN | Read forbidden |
| 3 | SWD_PWD_WR_Forbidden_EN | Write forbidden |
| 8 | SWD_Protection_Enable | Enable SWD protection |
| 9–12 | SWD Secure/NS × Noninvasive/Invasive Debug Disable | Disable |
| 2 | RSIP_KEY_RD_Forbidden_EN | Read forbidden |
| 5 | RSIP_KEY_WR_Forbidden_EN | Write forbidden |
| 6 | SBOOT_PK_WR_Forbidden_EN | Write forbidden |
| 13 | SECURE BOOT EN | Enable secure-boot ECC check |
| 14 | CPU_PC_DBGEN | （1 = 禁止调试口读 PC） |

> 注意:User2/User3(HUK,`0x150`)的"secure-only"是**硬件 TrustZone 访问控制**,不在这张表里(没有
> 专门的 User 读锁位);RSIP/RDP/SWD/SBOOT 才有 OTP 读/写锁位。

---

## 厂家视角:谁该留、谁该焚、谁该烧

| 材料 | 在哪 | 厂家怎么处理 |
|---|---|---|
| **SBOOT 签名私钥** | 片外 | **HSM/签名服务里重点保管**,泄露=能签恶意固件 |
| SBOOT 公钥(Ed25519) | eFuse `0x1A0` | 公开,烧进每片(可全产品相同) |
| **RSIP KEY** | eFuse `0x190` | 量产权衡:**每片唯一**(安全,但不能预构一份加密镜像给所有片)/ **产品级共享**(简单,但共享秘密较弱) |
| RSIP IV | imagetool | Realtek 给(NDA),build 时用;实际值见 §一 ⚠️ 框,转 public 前删 |
| RDP KEY | eFuse `0x170` | 同 RSIP 的权衡(每片/产品级) |
| **HUK** | eFuse `0x150` | **每片随机(CSPRNG)、烧完即焚、零留存**;只做设备本地用途 |
| IAK 私钥 | 设备内 | 设备内生成/注入即焚,厂家留**公钥** |
| SWD PWD | eFuse `0x180` | 量产烧 + 烧读/写锁位关调试(看是否要保留量产调试通道) |

**总原则**:芯片里放**公钥 / 设备本地密钥**;厂家私钥库里只放**签名私钥**这类"能造假"的东西,
**绝不留设备 HUK/私钥的副本**。

---

## 五、本测试板实烧记录（2026-06-25，bench 板已 provisioned + secure boot 已启用）

> ⚠️ 一次性不可逆，已实机落地（commit `4947406` 烧录 / `9a4b96d` 签名管线 / `afa7d38` DEBUG 门控）。
> 烧录的真实值记在 [`doc/test_device_keys.csv`](test_device_keys.csv)（private repo，每片一行）。

**烧进 eFuse 的值（全部 `efuse_burn` 写完即读回 MATCH）：**

| 区 | eFuse | 值（本板） | 启用？ |
|---|---|---|---|
| HUK | `0x150` 32B | `577c9a71…a248`（CSPRNG 随机） | — TF-M PS 根，硬件 TZ-secure |
| RDP key | `0x170` 16B | `ff…ff`（**空，未用**） | 否 |
| SWD PWD | `0x180` 16B | `cba13dec…7743`（随机） | **否**（SWD 保护位没烧，仅占位） |
| RSIP key | `0x190` 16B | `242dffb1…95f7`（随机） | **是**（2026-06-25 启用，见「RSIP 启用」） |
| SBOOT PK | `0x1A0` 32B | `ff79674e…7358`（= build/sboot Ed25519 公钥） | 是（见下） |

**启用的只有 image2 签名校验**：烧 `PROT 0x1C1 = 0x7f`，**只清 bit7**（= 字 bit15 = `Secure Img2 Enable`
= bootloader 的 `BIT_SECURE_IMG2_DIS`，`boot_flash_hp.c:558`）；`0x1C0` 不动、其余保护位全留空白值。
结果 `PROT` 字 `0xffff → 0x7fff`。**没烧**（与 RSIP 独立）：IMG1 `Secure Boot Enable`(bit5)、
SWD 保护、debug-disable —— 全留空 = UART(`flash.ps1`) + JTAG/OpenOCD 恢复路都开着。

**RSIP 启用（2026-06-25 实机验证，commit 5872112）**：RSIP + secure boot 在主板上实测共存可用。
- **enable 位 = 逻辑 eFuse `0xE[2]`（AN0405），不是物理 `0x19[5]`**（otf.h 注释误导：实烧物理 0x19[5]
  后 `syscfg3` 纹丝不动）。逻辑位要走 ROM 逻辑图写 `efuse_logical_write`（`EFUSE_LMAP_WRITE`，会重打包
  物理段），不能裸物理烧。本板：`efuse_lwrite e 1c CONFIRM`（逻辑 0xE `0x18`→`0x1c`，置 bit2、保留 bit3/4）。
- **加密镜像必须先签名再加密**（bootloader 验的是 OTF 解密后的明文 image2 + 从解密区读 SB_HEADER）：
  build_image_all.py 改成 sign→EncTool 加密（CMake 只加密 bootloader）；旧「先加密再签名」必 IMG2 SBOOT FAIL。
- **RSIP key 字节序**：build keystring 是 eFuse `0x190` 字节的**倒序**（AN0405：keystring 末字节→0x190）。
  本板 eFuse=`242dffb1…95f7` → `make RSIP_KEY=f7954f24be5a0e1756e852edb1ff2d24`。IV=`90acd34e…`（NDA）。
- **实测**：逻辑写 0xE[2] 后未加密镜像启动变黑（OTF 把明文解成乱码）= RSIP 已开；签名+加密镜像（同 key）
  干净启动→WiFi+console 活=OTF 解密+secure boot 共同通过。`OTF EN` 同 `IMG2 SBOOT OK` 走 DBG_8195A 没被抓。
- **数据区 No-RSIP mask（commit 8235a7e，已解决）**：RSIP 开后运行时写的数据区会被 OTF 腐败（AN0405 §1.3）。
  `RSIP_Mask_Config`（`rtl8721d_bootcfg.c`）加了 entry1=LittleFS `0x300000`(240页)、entry2=TF-M ITS/PS
  `0x3F0000`(16页)→ 实测 **LittleFS mount OK + ITS/PS persistence PASS**。PS 自己做 HUK-AEAD，要看自己的密文。
- **AWS+TF-M+RSIP 镜像（commit 8235a7e，实机通）**：build_image_all.py 去掉 TFM+RSIP 挡板，IMG3(tfm_s) 也
  RSIP 加密 @ `0x0e400000`（无头、不签名，只加密）。`make AWS=ON RSIP=ON RSIP_KEY=f7954f24…` 实测：加密 flash
  启动→TF-M ITS/PS PASS（tfm_s 解密通）+ LFS mount OK + WiFi 起。**完整生产配置（签名+加密+数据 mask）可用**。
  注：本板 AWS endpoint+cert 在启用过程中随 LFS 重格式化丢了（ITS key 还在）→ 显示 NOT provisioned，要重新
  `set endpoint`+`set cert`（非 RSIP 问题）。
- **副**：曾误把物理 `0x19[5]`（0x61→0x41）当 enable 烧了一次 → 无害（syscfg3 不变、WiFi+AWS 正常）。
  **试图改回失败 = eFuse 是真 OTP 不是 EEPROM**：写 `0x19=0x61` 后 read-back 仍 `0x41`（MISMATCH）——
  已烧成 0 的位不能 un-burn 回 1（与高区"写 0xff→0x7f 只能清位"一致）。误烧位不可逆但无功能影响。

**烧录机制（DEBUG，默认不编，never-ship）**：`make EFUSE_DEBUG=ON` 才编进
- `efuse_burn <addr> <hex> CONFIRM` 控制台命令（secure 写经 TF-M `AMEBAD_IOCTL_EFUSE_WRITE` → ROM
  `EFUSEWrite8` secure alias `0x10100b1d` + secure ctrl `0x40000810`；写完用 `EFUSE_RAW`(`0x8721E2`) 读回核对）；
- 开机 dump 全 eFuse 密钥 + WiFi/BLE MAC（恢复用）；
- `Boot_Log_En`（bootloader 详细日志）。
默认 `EFUSE_DEBUG=OFF`：这些全不在镜像里（NS 调用方没了 → SPE 的 EFUSE_RAW/WRITE IOCTL 无人能调）。

**secure boot 强制力实锤（非破坏性反证）**：用**错误密钥**签的镜像（pub≠eFuse）被 bootloader 拒、
**卡死在 `IMG1 ENTER` 到不了 main**；正确密钥（build/sboot = eFuse PK）正常 `IMG2 ENTRY → main → AWS`。
"看不到 `IMG2 SBOOT OK`"只是它走 `DBG_8195A` 没被串口抓、**不是没验签**。死循环后 UART 重烧签名镜像即恢复
（ROM 下载在验签之前）。

**签名私钥管理（关键）**：这把 Ed25519 私钥 = `build/sboot/`（gitignored），种子备份在 CSV `sboot_privkey`
列（`6937e25c…`）。`Makefile SBOOT ?= ON` 默认开 → 每次 `make` 自动签（熔丝板上未签名镜像 = `IMG2 SBOOT
FAIL` 死循环）。换机 / `distclean` 后 keygen 会拍**新随机 key**（熔丝板不认）→ 先跑
`python3 component/amebad/image_tool/sboot_restore.py`（读 CSV 种子重建 `build/sboot/`）**再** `make`。
生产应把这把私钥归 **HSM**，别只靠 `build/sboot/`。

**HUK 换根副作用**：烧 HUK 后首次重启 TF-M **PS 重格式化**（first-boot），但 **ITS 存活** → AWS 设备私钥
没丢、MQTT 自动重连，**不 brick、不用重新 provision**（ITS 非 HUK 加密、PS 才是）。

---

## 已核实来源 / 待精确化

- **Secure Boot = AN0410 已核实**:Ed25519、公钥本体 32B@`0x1A0`、ROM 校验失败死循环、三方流程、
  enable 位 `0x1C1[5]`(=逻辑 `0x1C0` bit13)、私钥在 `key_pair.txt`(厂家保管)。`Secure Image2` 也走
  签名校验(独立 enable 位)。
- **RSIP = AN0405 已核实**:AES-**128**(key 16B@`0x190`)、加密**镜像**、OTF 读解密、IV 找 Realtek;
  **运行时写不自动加密 → 数据区要设 No-RSIP mask(明文落盘)**;Enable 位逻辑 `0xE[2]`。**唯一没公开的是
  具体 AES 模式**(AN0405 原文"algorithm is specified by Realtek")——但用途/覆盖/key 长度都清楚了。
- **RSIP IV 实际值**:Realtek FAE 私发(NDA),记在 §一 的 ⚠️ 框 + `image_tool/CMakeLists.txt` `AMEBAD_RSIP_IV`
  默认值;本仓 private 故可记,**开源化前删**。
- **重要纠正**:之前说"开 RSIP 全盘加密后 LittleFS 数据也加密落盘"是**错的**。RSIP 只加密镜像(代码);
  LittleFS 这种运行时写的数据**必须 No-RSIP = 明文**。要数据落盘机密性,仍得 PS/app 软件加密(根在 HUK)。
- 本端 secure-boot 由 **Ameba ROM**(IMG1 校验)负责,TF-M 的 `BL2=OFF`,所以 TF-M ROTPK/MCUboot
  这套不参与;镜像真伪根在第二类(Ed25519)。
- 烧任何 eFuse 都是**一次性不可逆**,写错 API/区域**可能损坏芯片**(SDK 警告)。
- **本测试板已实烧完成并验证**(2026-06-25,见 §五):HUK/SWD/RSIP key/SBOOT PK 已烧、image2 secure boot
  已启用并实测强制(错误密钥镜像被拒)、私钥可从 CSV 种子用 `sboot_restore.py` 重建。RSIP/SWD 保护/debug
  关闭**故意未启用**(留恢复路)。
