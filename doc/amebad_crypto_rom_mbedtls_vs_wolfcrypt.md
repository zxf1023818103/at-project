# AmebaD KM4 — ROM mbedTLS vs wolfCrypt 不同 CPU 频率性能对比（含 Thumb2 / SRAM 优化）

在 AmebaD（RTL8721D）KM4（Cortex-M33）上，对比 **A-cut ROM mbedTLS 2.4.0**（纯软件路径，
`use_hw_crypto_func=0`）与 **wolfCrypt v5.9.1** 在相同算法、相同负载下的性能，分别测
KM4 CPU 频率 **200 / 100 / 50 / 25 MHz**；并测两种 wolfCrypt 优化：**Thumb2 对称汇编**、
**把对称代码+查表表放进 SRAM**。

测试代码：[component/amebad/app_km4/wolfcrypt_bench.c](../component/amebad/app_km4/wolfcrypt_bench.c)
里的 `crypto_clock_bench()`（日志前缀 `[CCB]`）。相关：[amebad_wolfssl_hwaccel_eval.md](amebad_wolfssl_hwaccel_eval.md)。

## 结论先行（TL;DR）

- **ROM mbedTLS** 跑在零等待周期的掩膜 ROM → cyc/B 恒定 → 吞吐随频率线性。
- **wolfCrypt 可移植 C（默认，住 XIP flash）** → cyc/B 随频率升高（flash 等待）→ 亚线性，
  查表密集的 AES 在 200 MHz 高达 **614 cyc/B**。
- **优化一：Thumb2 对称汇编**（`WOLFASM=ON`）—— 寄存器化实现、几乎不碰 flash 查表 → cyc/B
  恒定且最低：**AES 44 cyc/B（比 ROM 快 2.3×、比可移植C-flash 快 13.8×）**，SHA-256 50 cyc/B（≈ROM）。
  **不额外占 SRAM。最佳方案。**
- **优化二：对称代码+表放进 SRAM**（改 linker）—— 消除 flash 等待，AES 从 614 拍平到 **84 cyc/B
  （反超 ROM 的 102）**；但 SHA-256 本就偏计算、收益小（79→71）。代价：约 **+18 KB SRAM**。
- 排序（AES-128-CBC @200 MHz，cyc/B 越低越快）：**Thumb2 44 < SRAM 84 < ROM 102 ≪ 可移植C-flash 614**。

## 测试设置

| 项 | 值 |
|---|---|
| SoC / 核 | RTL8721D AmebaD，KM4 = Cortex-M33（NS，driver-only 镜像） |
| 频率 | `SystemSetCpuClk()` 200 / 100 / 50 / 25 MHz |
| ROM mbedTLS | A-cut ROM mbedTLS 2.4.0，软件路径；代码+查表表在**掩膜 ROM** |
| wolfCrypt v5.9.1 | 三种构建：① 可移植 C（住 XIP flash，默认）；② Thumb2 对称汇编（`WOLFASM=ON` → `AMEBAD_WOLFSSL_ARMASM`）；③ 可移植 C + `aes.c`/`sha256.c` 的 `.text`/`.rodata` 经 linker 放进 **SRAM**（`.ram_image2.text`） |
| 计时 | SysTick CPU 周期计数器（`cyc_now()`）；每次递减 = 1 CPU 周期，跨频率切换仍正确 |
| 负载 | SHA-256 = 32× 一次性 4 KB 哈希（128 KB）；AES-128-CBC = 128 KB 链式加密 —— 各构建等量工作 |
| 加锁 | ROM mbedTLS 与 WiFi 驱动共用 crypto 引擎 / ROM ssl-ram-map，故 ROM-mbedTLS 全程持 `device_mutex_lock(RT_DEV_LOCK_CRYPTO)`（加锁在计时区间外）。wolfCrypt 独立 |
| 指标 | `cyc/B` 与频率无关（CPU 效率，越低越快）；对 flash 内代码含 flash 等待周期 |

## 测试结果（cyc/B，越低越快）

### SHA-256

| CPU 频率 | ROM mbedTLS | wolf 可移植C（flash） | wolf 可移植C（SRAM） | wolf Thumb2 |
|--------:|-----------:|---------------------:|--------------------:|-----------:|
| 200 MHz | 46.9 | 79.4 | 70.7 | **50.1** |
| 100 MHz | 46.7 | 69.8 | 64.6 | 48.6 |
|  50 MHz | 47.0 | 65.0 | 63.1 | 47.2 |
|  25 MHz | 47.2 | 62.6 | 61.6 | 47.5 |

### AES-128-CBC（加密）

| CPU 频率 | ROM mbedTLS | wolf 可移植C（flash） | wolf 可移植C（SRAM） | wolf Thumb2 |
|--------:|-----------:|---------------------:|--------------------:|-----------:|
| 200 MHz | 102.3 | 614.1 | 84.3 | **44.4** |
| 100 MHz | 102.9 | 335.9 | 83.7 | 41.3 |
|  50 MHz | 102.5 | 206.0 | 83.8 | 39.9 |
|  25 MHz | 103.2 | 141.7 | 84.8 | 39.7 |

### 吞吐 @200 MHz（KB/s，越高越快）

| 算法 | ROM mbedTLS | wolf 可移植C（flash） | wolf 可移植C（SRAM） | wolf Thumb2 |
|---|-----------:|---------------------:|--------------------:|-----------:|
| SHA-256      | 4170 | 2458 | 2762 | 3897 |
| AES-128-CBC  | 1908 |  318 | 2317 | **4399** |

## 分析

### 1. 基线：ROM 频率无关，flash 不是
ROM mbedTLS cyc/B 恒定（SHA ~47，AES ~102）——掩膜 ROM 无等待周期，吞吐随频率 1:1。
wolfCrypt 可移植 C 住 XIP flash，flash 访问延迟在实际时间上固定（几十 ns）：200 MHz
（5 ns/周期）下相当于多个 CPU 等待周期，25 MHz（40 ns/周期）下约 1 个 → 同代码高频更"贵"，
吞吐亚线性。AES 是极端：每轮大量 T 表查表（~1 KB+ 的 `.rodata` 住 flash，取指 cache 帮不上
数据读）→ 200 MHz 下 **614 cyc/B**，25 MHz 降到 142。

### 2. 优化一 —— Thumb2 对称汇编（推荐）
Thumb2 AES 是寄存器化/计算式实现，几乎不碰 flash 查表 → cyc/B **恒定且最低**（44→40，基本平），
即已是 **CPU 瓶颈而非 flash 瓶颈**。AES-128-CBC 从可移植C-flash 的 614 直接到 **44 cyc/B**
（13.8×），并**反超 ROM（102）约 2.3×**；SHA-256 从 79 到 50（≈ROM 的 47）。**不额外占 SRAM。**

### 3. 优化二 —— 代码+表放 SRAM
把 `aes.c`/`sha256.c` 的 `.text` 与 `.rodata`（含 AES T 表）从 flash 搬到 SRAM，消除 flash 等待：
AES-128-CBC **拍平到 ~84 cyc/B（全频率持平）**，从 614（@200）降下来（7.3×），且**反超 ROM（102）**
——说明可移植 C 的 T 表 AES 一旦表在快速 SRAM 里，比 ROM 实现还快；之前的 614 几乎全是 flash 惩罚。
SHA-256 本就偏 ALU 计算、查表少（仅 64-word K 表），SRAM 收益小（79→71 @200，25 MHz 几乎无变化）。
代价：`.ram_image2.text` 约 **+18 KB SRAM**。

### 4. 谁最快
- **AES-128-CBC**：Thumb2(44) > SRAM(84) > ROM(102) ≫ 可移植C-flash(614)。Thumb2 完胜且零 RAM 代价。
- **SHA-256**：ROM(47) ≈ Thumb2(50) > SRAM(71) > 可移植C-flash(79)。SHA 偏计算，flash 惩罚小，
  各方案接近；ROM 与 Thumb2 并列最快。

## 建议

- **批量对称加密（AES）**：用 **wolfCrypt Thumb2**（`make WOLFASM=ON`）—— 最快、且不占 SRAM。
  若因故只能用可移植 C，则把 AES 代码+表放 SRAM 可把 614→84，但仍逊于 Thumb2 且要 ~18 KB RAM。
- **哈希（SHA-256）**：ROM mbedTLS 与 Thumb2 wolfCrypt 并列最快；纯软件 ROM 路径不占 flash/RAM，
  做批量哈希很划算。
- **公钥**（ECC P-256 / X25519 / RSA-2048）：只能用 wolfCrypt（SP Cortex-M 汇编）；本芯片 ROM
  mbedTLS 2.4.0 不暴露公钥。见 `[WCB]` 跑分。
- ROM mbedTLS 与 WiFi 并发时务必持 **`device_mutex_lock(RT_DEV_LOCK_CRYPTO)`**（共享 ROM ram-map）。

## 复现

- 基线 / Thumb2：`cd project/amebad && make`（可移植 C）或 `make WOLFASM=ON`（Thumb2）。
- SRAM 实验：在 `app_km4/rlx8721d_img2_is.ld` 的 `.ram_image2.text` 段把 `*aes.c.obj(.text* .rodata*)`
  / `*sha256.c.obj(...)` 放在 `.xip_image2.text` 通配之前（先匹配先入 RAM），再 `make`（可移植 C）。
- `crypto_clock_bench()` 在 WiFi 连接成功后跑；抓 1.5 Mbps LOG UART，grep `[CCB]`。cyc/B 确定性，
  单次即可；KB/s 带少量 SysTick 取整噪声。
