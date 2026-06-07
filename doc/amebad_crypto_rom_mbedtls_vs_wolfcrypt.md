# AmebaD KM4 — ROM mbedTLS vs wolfCrypt 不同 CPU 频率性能对比

在 AmebaD（RTL8721D）KM4（Cortex-M33）上，对比 **A-cut ROM mbedTLS 2.4.0**（纯软件路径，
`use_hw_crypto_func=0`）与 **wolfCrypt v5.9.1** 在相同算法、相同负载下的性能，分别测
KM4 CPU 频率 **200 / 100 / 50 / 25 MHz**。

测试代码：[component/amebad/app_km4/wolfcrypt_bench.c](../component/amebad/app_km4/wolfcrypt_bench.c)
里的 `crypto_clock_bench()`（日志前缀 `[CCB]`）。相关：wolfCrypt 单独全算法跑分
`wolfcrypt_bench()`（`[WCB]`）、[amebad_wolfssl_hwaccel_eval.md](amebad_wolfssl_hwaccel_eval.md)。

## 结论先行（TL;DR）

- **ROM mbedTLS 跑在零等待周期的掩膜 ROM 里 → cyc/B 几乎不随频率变 → 吞吐随 CPU 频率近似线性扩展。**
- **wolfCrypt 跑在 XIP flash 里 → cyc/B 随 CPU 频率"升高"（取指/查表的 flash 等待周期变多）→
  吞吐"亚线性"扩展；对查表密集的 AES 尤其惨烈。**
- 两个被测算法、所有频率点，ROM mbedTLS 都更快；频率越高差距越大（主要是 flash 惩罚，不是算法本身）：
  SHA-256 快 1.3–1.7×，AES-128-CBC 快 **1.4–6.0×**。
- 这与其说是"密码库对比"，不如说是"**存储系统**对比"——在一个库住 ROM、另一个库住 XIP flash 的芯片上，
  高频时"代码/查表表放哪"才是主导因素。

## 测试设置

| 项 | 值 |
|---|---|
| SoC / 核 | RTL8721D AmebaD，KM4 = Cortex-M33（NS，driver-only 镜像） |
| 频率 | `SystemSetCpuClk()` 200 / 100 / 50 / 25 MHz |
| ROM mbedTLS | A-cut ROM mbedTLS 2.4.0，软件路径（`rom_mbedtls_init()`，`use_hw_crypto_func=0`）；代码+查表表在**掩膜 ROM** |
| wolfCrypt | v5.9.1，**对称/哈希 = 可移植 C**，公钥 = SP Cortex-M 汇编；代码+查表表在 **XIP flash**（`.xip_image2.text` / `.rodata`） |
| 计时 | SysTick CPU 周期计数器（`cyc_now()`）；每次 SysTick 递减 = 1 个 CPU 周期，故跨频率切换计数仍正确；KB/s 用已知频率换算 |
| 负载 | SHA-256 = 32 次一次性 4 KB 哈希（共 128 KB）；AES-128-CBC = 128 KB 链式加密 —— **两库等量工作** |
| 加锁 | ROM mbedTLS 与 WiFi 驱动共用 crypto 引擎 / ROM ssl-ram-map，故 ROM-mbedTLS 全程用 `device_mutex_lock(RT_DEV_LOCK_CRYPTO)` 串行化（加锁在计时区间之外）。wolfCrypt 独立，无需加锁 |
| `cyc/B` | 与频率无关的 CPU 效率指标（越低越快）；对 flash 内的代码包含 flash 等待周期 |

## 测试结果

### SHA-256

| CPU 频率 | ROM mbedTLS cyc/B | ROM mbedTLS KB/s | wolfCrypt cyc/B | wolfCrypt KB/s | ROM 提速（KB/s） |
|--------:|------------------:|-----------------:|----------------:|---------------:|----------------:|
| 200 MHz | 47.77 | 4088 | 79.43 | 2458 | 1.66× |
| 100 MHz | 46.70 | 2090 | 69.80 | 1399 | 1.49× |
|  50 MHz | 47.01 | 1038 | 64.96 |  751 | 1.38× |
|  25 MHz | 47.19 |  517 | 62.62 |  389 | 1.33× |

### AES-128-CBC（加密）

| CPU 频率 | ROM mbedTLS cyc/B | ROM mbedTLS KB/s | wolfCrypt cyc/B | wolfCrypt KB/s | ROM 提速（KB/s） |
|--------:|------------------:|-----------------:|----------------:|---------------:|----------------:|
| 200 MHz | 103.10 | 1894 | 614.07 | 318 | **5.96×** |
| 100 MHz | 102.93 |  948 | 335.93 | 290 | 3.27× |
|  50 MHz | 102.52 |  476 | 205.95 | 237 | 2.01× |
|  25 MHz | 103.47 |  235 | 141.69 | 172 | 1.37× |

## 分析

### 1. ROM 频率无关，flash 不是

ROM mbedTLS 的 cyc/B 几乎不随频率变（SHA-256 47.0–47.8，AES-128-CBC 102.5–103.5）。
掩膜 ROM 无等待周期，一次取指/查表在任何频率都是相同的周期数 → 吞吐与频率 1:1
（SHA-256 4088 → 517 KB/s，8× 降频对应 ÷7.9；AES 1894 → 235，÷8.06，近乎理想线性）。

wolfCrypt 的 cyc/B **随降频而下降**（SHA-256 79.4 → 62.6；AES-128-CBC **614 → 142**）。
它跑在 XIP flash 上，而 flash 访问延迟在"实际时间"上大致固定（几十 ns）。200 MHz
（5 ns/周期）下这点延迟相当于好几个 CPU 等待周期；25 MHz（40 ns/周期）下约 1 个。
所以同一段代码在高频下花更多 CPU 周期 → 吞吐**亚线性**扩展。

### 2. AES 是极端例子（T 表在 flash）

wolfCrypt 的可移植 C 版 AES 是查表实现：每轮大量 SBox / MixColumns 查表，落在 ~1 KB+
的 `.rodata`，而这块住在 **XIP flash**。每次查表都是一次 flash **数据**读（flash 的取指
cache 帮不上数据），等待周期占主导：**200 MHz 下 614 cyc/B**。降频后 flash 跟得上，掉到
142 cyc/B。最醒目的一条：

> wolfCrypt AES-128-CBC 吞吐在 200→50 MHz 几乎"持平"（318 → 237 KB/s，4× 降频仅 ÷1.34）
> —— 它是 **flash 等待瓶颈，不是 CPU 瓶颈**。同区间 ROM mbedTLS AES 则纯 CPU 瓶颈
> （1894 → 476，÷3.98）。

SHA-256 更偏 ALU 计算、查表内存访问少，flash 惩罚温和（200 MHz 下 cyc/B 79 vs 47），
基本仍随频率线性。

### 3. 谁更快

两个算法、每个频率点 ROM mbedTLS 都更快——但高频时的大部分优势来自存储系统，不是算法。
到 25 MHz（flash 惩罚最小）两库接近收敛（SHA-256 47 vs 63 cyc/B；AES 103 vs 142），
说明算法本身在同一量级；ROM 仍略胜，得益于是手工调优的 ROM 实现。

## 建议

- 本芯片上做**批量对称/哈希**优先用 **ROM mbedTLS 软件路径**——更快、随频率稳定、且不占 flash。
  （HW IPSEC 引擎在 NS driver-only 镜像里够不着，见 [reference-amebad-hw-crypto]。）
- 若必须用 wolfCrypt 做对称（ROM 缺的算法，或想避开与 WiFi 共享 ROM ram-map /
  `RT_DEV_LOCK_CRYPTO`），要预期高频下 XIP flash 的重惩罚。缓解：编 **Thumb2 对称汇编**
  档位（`WOLFSSL_AMEBAD_ARMASM`，查表更少更省），和/或把热点 AES 代码+查表表放进
  **SRAM**（`.ram_image2.text` / RAM `.rodata`），避开 flash 等待周期。
- **公钥**仍选 wolfCrypt（ECC P-256、X25519、RSA-2048）：本芯片 ROM mbedTLS 2.4.0 只暴露
  AES/SHA/MD5/HMAC，没有公钥。wolfCrypt 公钥时延见 `[WCB]` 那次跑分。
- ROM mbedTLS 与 WiFi 并发运行时，务必在其外层持 **`device_mutex_lock(RT_DEV_LOCK_CRYPTO)`**
  （ROM ssl-ram-map 与 WiFi 驱动的 crypto 共用）。

## 复现

`crypto_clock_bench()` 在 `wifi_bringup_thread()` 里 WiFi 连接尝试之后调用（与链路无关，
连接失败也会跑）。按 1.5 Mbps 抓 LOG UART，grep `[CCB]`。cyc/B 是确定性的，单次即可；
KB/s 带少量 SysTick 取整噪声。

> 运行条件：KM4 driver-only NS 镜像；wolfCrypt 对称档位 = 可移植 C（见 `[WCB]` banner）；
> 换成 Thumb2 汇编档会缩小 AES 的差距。
