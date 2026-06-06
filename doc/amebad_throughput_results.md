# AmebaD (RTL8721D / RTL8720DN) KM4 — 测速结果汇总

KM4 (Cortex-M33 @ 200 MHz) image2 上两类实机测速结果:**网络吞吐**(TCP/UDP,iperf2 over
FreeRTOS+TCP)与 **加密吞吐**(SSL/wolfCrypt v5.9.1)。两者都是 driver-only image、纯软件
(无硬件加速引擎——SEC 引擎在本 image 调不通,见 [[reference_amebad_hw_crypto]])。

---

## 1. 网络吞吐(TCP / UDP,iperf2)

- **栈**:FreeRTOS-Plus-TCP V4.0.0(替 lwip),`component/amebad/freertos_tcp/`;Realtek 闭源
  `lib_wlan.a` ↔ 栈 copy-based 桥。测具:`app_km4/iperf_{client,server,udp_client,udp_server}.c`
  设备端自报 + host `iperf -s` / `iperf -u -s` 双读数一致。
- **链路**:连 5 GHz AIoT(BSSID `9c:9d:7e:59:3e:84`,ch40),RSSI -41 dBm,RTT 4 ms,
  power-save 全关(`wifi_set_power_mode(0,0)`),KM4 跑满 200 MHz。

| 方向 | 协议 | 吞吐 | pps | 备注 |
|---|---|---|---|---|
| TX (device→host) | TCP | **11.0–11.1 Mbps** | ~930 | 5G 极稳(11.10/11.10/11.07);2.4G 抖 7–12 |
| TX (device→host) | UDP | **22.7 Mbps** | 1930 | 驱动 skb 池被打满(skb_unavailable=673) |
| RX (host→device) | TCP | **10–12 Mbps** | — | 与 TX 同档 |
| RX (host→device) | UDP | ~15 Mbps | 1300 | 1300 pps @1470 B(>1000 → 非 tick 量化) |

**结论:~11–12 Mbps 是本栈 + copy-based WiFi 桥在 200 MHz M33 上的确定性软件天花板,不是空口。**
(5G 干净、RTT 4 ms、RSSI -41 都很好;5G 只消了 2.4G 拥挤抖动,没抬上限。)

根因(三条 pps 钉死):UDP TX 1930 | UDP RX 1300 | TCP TX 930。
- 单 FreeRTOS+TCP **IP task 是每包处理上限**。TCP TX 每段串行付一次 send(~0.52 ms)+ ~半次
  延迟-ACK 的 RX,≈ 1/(0.52+0.5·0.77) ms ≈ ~1100 pps,吻合实测 ~930。
- 每包 ~0.5–0.8 ms(~100–150k cycles)花在闭源 `rltk_wlan_send/recv` 路径,**不是** copy(~1%)/
  malloc(µs 级)/ window / tick。
- **window 不是杠杆**:`lTxWinSize` 12→32 反降(10.9→9.7 Mbps,RTT 13→38 ms 纯 bufferbloat)。
- **tick 不是根因**:UDP RX 1300 pps > 1 kHz,"~1 包/tick" 是 930≈1000 的巧合。
- **零拷贝 buffer 否决**:BufferAllocation_1 静态 buffer 的 61 KB BSS 溢进闭源驱动硬占的 SRAM →
  wifi_connect 即挂;且 malloc 是 µs 级,治不了 100k-cycle/包的驱动开销。回退 BufferAllocation_2。
- **带宽走 UDP**(22.7 Mbps)有余量;要把 TCP 冲到 ~17 Mbps 得做零拷贝 TX/RX 桥
  (`rltk_wlan_alloc_skb`/`send_skb` 免 copy + 拆 IP-task 单线程 per-packet 开销),工程量大。

出处:commits `53f44f5`(5G 稳态)、`fcbd7b1`(UDP-vs-TCP 判别)、`79799ec`(真因定论)。

---

## 2. 加密吞吐(SSL / wolfCrypt v5.9.1)

实机跑 `app_km4/wolfcrypt_bench.c`(SysTick 派生 CPU 周期计时,实测基频 200.000 MHz;DWT CYCCNT
在本 NTZ image 读 0,被 secure-gate)。三档:portable C / SP Cortex-M 汇编(PK)/ Thumb2 汇编(对称)。
完整 HW-加速评估与 `user_settings.h` 见 [[amebad_wolfssl_hwaccel_eval]](`doc/amebad_wolfssl_hwaccel_eval.md`)。

**对称/哈希吞吐(KB/s,越大越好):**

| 算法 | portable C | Thumb2 asm | 提速 |
|---|---|---|---|
| AES-128-GCM       | 338  | **2083** | **6.0×** |
| SHA-256           | 3352 | 4160 | 1.25× |
| SHA-512           | 1660 | 2072 | 1.25× |
| ChaCha20-Poly1305 | 5179 | 6132 | 1.18× |

**公钥/握手(单次耗时,越小越好):**

| 操作 | portable C | SP Cortex-M asm | 提速 |
|---|---|---|---|
| ECDSA P-256 keygen | 64.6 ms | **5.2 ms**  | **12.4×** |
| ECDSA P-256 sign   | 71.5 ms | 7.7 ms      | 9.3× |
| ECDSA P-256 verify | 130.1 ms| 15.0 ms     | 8.7× |
| ECDHE P-256 agree  | 64.6 ms | **10.2 ms** | 6.3× |
| RSA-2048 verify    | 12.5 ms | 7.3 ms      | 1.7× |
| RSA-2048 sign      | 702 ms  | 269 ms      | 2.6× |
| X25519 keygen/agree| 8.9 ms  | **4.5 ms**(Thumb2)| 2.0× |

**结论**:纯 CPU 汇编(绕开死掉的 SEC 引擎)就把 MCU 上的 TLS 握手做实用了——一次
ECDHE-P256 + RSA-2048-verify 的非对称开销 ≈ **17.5 ms**(汇编)vs ~77 ms(纯 C);记录层
AES-GCM 6× / X25519 2×。flash 代价 ~+100 KB text(image2 487→590 KB)。

复现:`cmake build/km4 -DAMEBAD_WOLFSSL_SP_ASM=<ON|OFF> -DAMEBAD_WOLFSSL_ARMASM=<ON|OFF>`
→ `python3 component/amebad/image_tool/build_image_all.py` → flash → 看串口 `[WCB]` 行。

---

## 速览

| 维度 | 数 | 限制因素 |
|---|---|---|
| TCP TX/RX | ~11–12 Mbps | 单 IP-task + copy 桥 per-packet(闭源驱动 ~100k cyc/包) |
| UDP TX | 22.7 Mbps | 驱动 skb 池(无 ACK clock) |
| TLS 握手(ECDHE+RSA-verify) | ~17.5 ms | SP Cortex-M 汇编后受 RSA/ECC 标量乘 |
| AES-128-GCM | 2.0 MB/s | Thumb2 汇编(M33 无 AES 指令) |
| ChaCha20-Poly1305 | 6.0 MB/s | 纯 C 已最快,M33 上的首选 AEAD |
