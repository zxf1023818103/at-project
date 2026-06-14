# AmebaD KM4 — 上游 Mbed TLS v3.6.4 vs wolfSSL/wolfCrypt 跑分对比

`experiment/mbedtls-picotls-migration` 分支把项目的 TLS/QUIC 加密从 **wolfSSL** 迁到
**上游 Mbed TLS v3.6.4 + picotls**。本文是迁移的性能依据：在同一颗 RTL8721D KM4
(Cortex-M33 @200 MHz) 上，用**相同算法、相同 512 KB 负载、相同 SysTick 周期计数器**，
对比 Mbed TLS 3.6.4 (`[MCB]`) 与 wolfCrypt v5.9.1。

- mbedTLS 跑分：[component/amebad/app_km4/mbedtls_bench.c](../component/amebad/app_km4/mbedtls_bench.c)（`make BENCH=ON`，日志 `[MCB]`），**实机实测（本文）**。
- wolfCrypt 对称数：[amebad_crypto_rom_mbedtls_vs_wolfcrypt.md](amebad_crypto_rom_mbedtls_vs_wolfcrypt.md)（`[CCB]`）。
- wolfCrypt 公钥数：[amebad_wolfssl_hwaccel_eval.md](amebad_wolfssl_hwaccel_eval.md)（`[WCB]`）。
- 两边都是**软件实现**（无 RTL8720DN 硬件加密引擎——见 reference 记忆，那引擎在 driver-only 镜像里调不通）。

## 结论先行（TL;DR）

- **对称（同为可移植 C / 住 XIP flash 的"公平"对比）：mbedTLS 3.6.4 完胜 wolfCrypt 可移植 C。**
  AES-128-CBC **148.9 vs 614.1 cyc/B（4.1×）**，SHA-256 **50.0 vs 79.4（1.6×）**。mbedTLS 的查表更小、
  flash 惩罚轻得多。
- **但 wolfCrypt 开了 Thumb2 汇编后反超 mbedTLS：** AES-128-CBC **44.4 vs 148.9（3.4×）**；SHA-256 则**打平**（50.1 vs 50.0）。
- **公钥定点运算（keygen / ECDSA sign）mbedTLS 更快**（comb 预计算）：ECDSA-P256 sign **38.7 vs 71.5 ms（1.8× 快过 wolf 可移植 C）**。
- **公钥变基运算（ECDH agree / ECDSA verify）mbedTLS 更慢**：ECDHE-P256 agree **118 vs 64.6 ms（慢 1.8×）**。
- **X25519 是 mbedTLS 的明显短板**：**88–94 ms vs wolf 8.9 ms（≈10× 慢）**——mbedTLS 走通用 `ecp.c` 大数路径，没有专门的 25519 域算术。
- **wolfCrypt SP Cortex-M 汇编在所有公钥上碾压**（asm vs C，非同台）：ECDHE 10.2 ms、ECDSA-verify 15 ms、RSA-sign 269 ms。
- **迁移判断**：纯软件、可移植 C 下 mbedTLS 3.6.4 对称更优、ECC 定点更优；代价是 **X25519 慢** + **变基 ECC 慢** + **AES 不如 wolf 的 asm**。对 TLS1.3/QUIC 握手（ECDHE + 证书验签）属于可接受范围，但若卡握手延迟，需补 mbedTLS 的 Cortex-M 加速（见"后续"）。

## 测试设置

| 项 | 值 |
|---|---|
| SoC / 核 | RTL8721D AmebaD，KM4 = Cortex-M33（NS，driver-only 镜像），**200 MHz** |
| Mbed TLS | **上游 v3.6.4**，软件；AES/SHA/ChaCha 为可移植 C，住 XIP flash；大数（RSA/ECC）用内置 `MULADDC` 内联汇编（`MBEDTLS_HAVE_ASM`） |
| wolfCrypt | **v5.9.1**，三构建：可移植 C（住 flash）/ Thumb2 对称汇编 / SP Cortex-M 公钥汇编 |
| 计时 | SysTick CPU 周期计数器；对称 512 KB 负载，公钥多次取平均 |
| 指标 | 对称 `cyc/B`（越低越快）；公钥 `ms/op`（越低越快） |

> **"公平"对比 = 可移植 C vs 可移植 C**（都住 flash、都无手写 asm）。wolfCrypt 的 Thumb2/SP-asm 列是
> wolfSSL 在本项目实际启用的配置（`make WOLFASM=ON`），列出作参考——但那是 asm vs C，不同台。

## 对称 / 哈希（cyc/B，越低越快；@200 MHz）

| 算法 | **mbedTLS 3.6.4** | wolf 可移植C(flash) | wolf Thumb2(asm) | wolf C(SRAM) | ROM mbedTLS 2.4.0 |
|---|---:|---:|---:|---:|---:|
| SHA-256          | **50.0** | 79.4 | 50.1 | 70.7 | 46.9 |
| SHA-512          | **99.5** | — | — | — | — |
| AES-128-CBC enc  | **148.9** | 614.1 | 44.4 | 84.3 | 102.3 |
| AES-128-GCM enc  | **465.6** | — | — | — | — |
| ChaCha20-Poly1305| **79.7** | — | — | — | — |

吞吐 @200 MHz（KB/s）：mbedTLS SHA-256 **3904**、SHA-512 1963、AES-128-CBC **1311**、AES-128-GCM 419、ChaCha20-Poly1305 2450。

## 公钥（ms/op，越低越快；@200 MHz）

| 操作 | **mbedTLS 3.6.4** | wolf 可移植C | wolf SP-asm | mbedTLS 优于 wolf-C? |
|---|---:|---:|---:|:--:|
| ECDSA P-256 keygen | **34.9** | 64.6 | 5.2 | ✅ 1.9× |
| ECDSA P-256 sign   | **38.7** | 71.5 | 7.7 | ✅ 1.8× |
| ECDSA P-256 verify | 124.5 | 130.1 | 15.0 | ≈ 持平 |
| ECDHE P-256 agree  | 118.1 | 64.6 | 10.2 | ❌ 慢 1.8× |
| X25519 keygen      | 88.8 | 8.9* | 4.5 | ❌ **慢 ~10×** |
| X25519 agree       | 94.1 | 8.9* | 4.5 | ❌ **慢 ~10×** |
| RSA-2048 sign(priv)| **447.8** | 702 | 269 | ✅ 1.6× |
| RSA-2048 verify(pub)| 12.9 | 12.5 | 7.3 | ≈ 持平 |

> mbedTLS op/s 实测：ECDSA sign 25、verify 8；ECDHE 8；X25519 keygen 11 / agree 10；RSA sign 2、verify 77。
> `*` wolf 的 X25519 是 keygen/agree 合并记的 8.9 ms（[WCB]），单 op 量级。

## 分析

### 1. 对称：mbedTLS 的可移植 C 对 flash 友好得多
wolfCrypt 可移植 C 的 AES 用大 T 表（~1 KB+ `.rodata` 住 flash），@200 MHz 每次查表吃 flash 等待 →
**614 cyc/B**。mbedTLS 3.6.4 的 AES 查表小（FSb + 紧凑实现）→ **149 cyc/B**，同为 flash-resident C 却快 4.1×。
SHA-256 两边都偏 ALU、查表少，mbedTLS（50）仍优于 wolf-C（79），且**追平 wolf 的 Thumb2 asm（50.1）和 ROM（47）**——
mbedTLS 3.6 的 SHA-256 C 实现已接近手写 asm。**但 AES 上 wolf Thumb2（44）/SRAM（84）仍胜 mbedTLS（149）**：纯 C 的 AES 打不过寄存器化 asm。

### 2. 公钥：定点快、变基慢——mbedTLS 的非对称特性
mbedTLS ECDSA **keygen/sign 比 wolf-C 快 ~1.8×**（34.9/38.7 vs 64.6/71.5），但 **ECDH agree/ECDSA verify 慢或持平**
（118/124 vs 64.6/130）。原因：mbedTLS 对**生成元定点**标量乘有 comb 预计算（keygen、sign 的 k·G 受益），
而**变基**标量乘（ECDH 的 peer 点、verify 的 Q 点）走通用路径、无预表 → 慢。wolfCrypt 可移植 C 对两类更均匀。
RSA-2048 sign mbedTLS 反而快（447 vs 702，CRT + 大数 MULADDC asm 帮忙）；verify 持平。

### 3. X25519 是 mbedTLS 的真短板
mbedTLS 的 Curve25519 走**通用 `ecp.c` 蒙哥马利曲线 + 通用 bignum**，没有专门的 2^255-19 域约简 →
**88–94 ms**，比 wolfCrypt 专用 curve25519 C（8.9 ms）**慢约 10×**。TLS1.3 默认偏好 X25519 群，这一项最该警惕。

### 4. asm 才是 wolfSSL 的杀手锏
wolfCrypt 一旦开 SP Cortex-M / Thumb2 asm（本项目 `WOLFASM=ON` 的实际配置），公钥全面碾压 mbedTLS 可移植 C
（ECDHE 10.2、verify 15、RSA-sign 269 ms）。**这是 asm vs C，不是算法优劣**——mbedTLS 也能上 Cortex-M 加速（见后续）。

## 对迁移的判断

- **够用**：纯软件下 mbedTLS 3.6.4 的对称（AES/SHA/ChaCha）和 ECDSA 定点运算都不输甚至优于 wolfCrypt 可移植 C；
  一次 TLS1.3/QUIC 握手的非对称开销以 P-256 计 ≈ ECDHE(118) + 证书 ECDSA-verify(124) ≈ **0.24 s**（软件、单核 M33），
  能接受但不快。
- **要注意**：① **X25519 慢 10×**——若 peer 偏好 X25519，握手会更慢，建议群偏好排序优先 P-256，或补 25519 加速；
  ② 变基 ECC（ECDH/verify）慢于 wolf-C；③ AES 吞吐不如 wolf 的 asm（419 KB/s GCM 对高吞吐 TLS 记录层是瓶颈）。
- **③ 已被端到端实测证实**：把 QUIC 从 ngtcp2+wolfSSL 迁到 ngtcp2+picotls/mbedTLS 后，同一 AES-128-GCM
  服务器下 LAN 吞吐**下载 4.53→1.72 Mbps（2.6× 慢）、上传 2.42→1.29 Mbps（1.9× 慢）**——回归量正好对应
  GCM 的 crypto 回归（mbedTLS SW-C GCM 419 KB/s vs wolfSSL asm-GCM ~2 MB/s）。详见
  [amebad_quic_test.md](amebad_quic_test.md) 的 2026-06-14 更新。

## 后续（如果要把 mbedTLS 拉到 wolf-asm 档位）

- **大数 asm**：确认 `MBEDTLS_HAVE_ASM` 已开（RSA/ECC 的 `MULADDC` Thumb 已在用——故 RSA/ECDSA-sign 不算太差）。
- **P-256 加速**：开 `MBEDTLS_ECP_DP_SECP256R1_ENABLED` 的专用快速约简 + 定点表；或接 PSA driver 走外部 ECC 实现。
- **X25519**：换用 mbedTLS 的 `MBEDTLS_ECP_DP_CURVE25519` 优化路径或外部 25519 实现（当前最大短板）。
- **AES**：纯 C 打不过 wolf Thumb2；如需高吞吐 TLS，考虑把 AES 表/代码放 SRAM（参考 [amebad_crypto_rom_mbedtls_vs_wolfcrypt.md](amebad_crypto_rom_mbedtls_vs_wolfcrypt.md) §优化二）或上 PSA AES driver。

## 复现

```bash
cd project/amebad && make BENCH=ON      # 产 build/amebad/firmware.bin（含 [MCB] 微基准）
# flash.ps1 -BootPort COM14 -FlashPort COM3 -Image firmware.bin -Baudrate 1500000
# 抓 COM3 @1.5 Mbaud，grep [MCB]；bench 在 WiFi 连接后跑一次（cyc/B 确定性，单次即可）
```

wolfCrypt 侧数据来自迁移前的 `[CCB]`/`[WCB]` 跑分（上面两个链接的 doc），同一 HW/时钟/计数器，可直接并列。
