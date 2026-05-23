# Bouffalo 网络栈方案评估

针对 bouffalo_sdk 的网络栈调整三个问题：抽出 lwIP 单独维护、换 FreeRTOS-Plus-TCP、切换 WiFi driver 路线（fhost / wl80211 / net80211）。本文是 nm 实证 + driver-only 实验分支的结论。

补充：三家 SDK 共抽 lwIP/FreeRTOS/mbedTLS 的评估在 [shared_components_eval.md](shared_components_eval.md)，结论是 lwIP 不抽、FreeRTOS 可抽、mbedTLS 优先抽——主题正交，不重复。

## TL;DR

- **抽 Bouffalo lwIP 单独维护**：可行性 9/10，必要性 3/10。建议只做 patch 归档，不物理迁移。
- **换 FreeRTOS-Plus-TCP 替换 lwIP**：技术难度 ★★★★。驱动层 .a **不锁 lwIP**（nm 扫描证实），但应用层 + buffer 模型 + 低功耗 patch 三道墙没拆。**不建议作为业务任务**，可作为研究性 POC。
- **WiFi driver 路线**：BL616 默认 fhost，但**也支持 wl80211**（`libwl80211_bl616.a` 存在 + `examples/wifi/macsw_bare/defconfig` 就是 BL616+wl80211）。换栈 POC **应走 wl80211 路径**——`wl80211/{lwip.c, nuttx.c}` 已经是 dual-stack 适配，第三份 `freertos_tcp.c` 是设计内的扩展。

---

## 一、抽出 bouffalo_sdk 的 lwIP 单独维护

### 数据

| 维度 | 现状 | 评价 |
|---|---|---|
| upstream 基线 | lwIP **2.1.2 release**（`src/include/lwip/init.h` 验证） | ✅ 整齐 |
| 私有 patch | **12 / 295 文件** 带 `bouffalo lp change` 标记 | ✅ <5% |
| patch 标记规范度 | `/* bouffalo lp change … */ … /* bouffalo lp change end */` 配对 | ✅ 可机械化提取 |
| port 层 WiFi 耦合 | [lwip-port/FreeRTOS/ethernetif.c](../platform/bouffalo_sdk/components/net/lwip/lwip/lwip-port/FreeRTOS/ethernetif.c) 是干净 skeleton，无 wifi/bl_wifi 引用 | ✅ |
| WiFi 胶水位置 | 独立组件 [components/wireless/wifi6/wifi6_lwip_adapter/](../platform/bouffalo_sdk/components/wireless/wifi6/wifi6_lwip_adapter/)，只 include `lwip/*.h`、`netif/*.h` 公开 API | ✅ 抽出后仍能链接 |
| lwipopts.h | 实际生效那份在 `wifi6_lwip_adapter/include/lwipopts.h`（不在 lwip/ 自己目录） | ✅ 配置随 SDK，抽出无冲突 |

12 个改动文件集中度极高（全是低功耗下 TCP/ARP/timer 行为修改，DTIM / WiFi PS 配套，**产品特性不能丢**）：
```
src/core/{timeouts,tcp,tcp_in,tcp_out,dns}.c
src/core/ipv4/{etharp,dhcp,ip4_frag}.c
src/include/lwip/{timeouts,tcp}.h
src/include/lwip/priv/tcp_priv.h
```

### 结论：必要性低

与 mbedTLS（三家共抽，强建议）的对比：

| | mbedTLS | bouffalo lwIP |
|---|---|---|
| 服务对象 | 3 家 SDK 共用 | 只服务 Bouffalo 一家 |
| 上游 diff | 几乎 0 | 12 个 patch（产品特性，无法上 upstream） |
| 抽出后 SDK 体内剩余 | HW alt 留下，干净 | 仍要保留 wifi6_lwip_adapter，物理减重为 0 |
| 主要收益 | 跨厂统一 + CVE 跟踪 | **只剩** CVE 跟踪 |

阻碍点：
1. bouffalo_sdk 是 git submodule（不 fork 政策），物理抽出后要么 SDK 体内残留一份重复（无意义），要么用 CMake include path 覆盖（脆弱）。
2. CMakeLists 依赖 SDK 私有 helper（`sdk_generate_library` / `sdk_library_add_sources`），抽出后要么沿用、要么改写。
3. 12 个低功耗 patch 升级时要每个手工 re-apply + WiFi PS 回归测试。

### 建议

**短期不抽，但把维护资产先建起来**：
- `at-project/patches/bouffalo_lwip/` 下用脚本生成 12 个 patch（对比 upstream 2.1.2 tag）。
- 索引出来即可，**物理不移动**。

时机：跟着三家 lwIP 共抽一起做（≥半年后），或 Bouffalo 出现严重 CVE 需要紧急 bump 时再做。

---

## 二、用 FreeRTOS-Plus-TCP 替换 lwIP

### 当前 lwIP 在 SDK 里的足迹

| 调查项 | 数据 |
|---|---|
| bouffalo_sdk 里直接 `#include "lwip/"` 的源文件 | **~74 个**（按 `wc -l` 去 lwip 自身和 test 后） |
| 覆盖范围 | wifi 适配层、AT 命令、HTTP/MQTT/iperf、TLS shim、OpenThread Border Router、power_mgmt |
| 我们 `src/main.cpp` 里的 lwIP 调用 | **仅 1 处**：`tcpip_init(on_tcpip_init_done, NULL)` |
| `src/Common.h` lwIP 引用 | 1 处：`#include <lwip/tcpip.h>` |

**关键认知**：我们自己写的代码几乎不绑 lwIP，但**整个 bouffalo_sdk 上层网络栈/AT 命令实现绑死 lwIP**。换栈影响面是 SDK 而非业务代码。

### FreeRTOS-Plus-TCP 与 lwIP 的不兼容（致命差异）

| # | 差异 | 影响 |
|---|---|---|
| 1 | 缓冲模型：lwIP `pbuf` 链式零拷贝 vs FreeRTOS+TCP `NetworkBufferDescriptor_t` 单段固定 | WiFi6 driver (`wifi6_lwip_adapter/net_al.c`、`pbuf_custom_ref.c`、`tx_buffer_copy.c`，~2000 行) 用 `pbuf_alloced_custom` 把 MAC 层 DMA buffer 当 pbuf 喂给 lwIP，换栈要全部重写为「driver → memcpy 到 NetworkBufferDescriptor_t」。RX 性能损失。 |
| 2 | 没有 `altcp_tls`（lwIP TLS shim） | bouffalo SDK 多个组件依赖 `lwip/altcp_tls.h`：`fota/https/`、`fota/tcp/`、`net/netbus/atmodule/src/at_http/`、`at_mqtt/`、`net/lib/http/`、`mqtt/`。FreeRTOS+TCP 阵营走 coreMQTT/coreHTTP + Transport Interface，**API 完全不同**。SDK 自带 HTTP/MQTT/HTTPS/OTA 客户端要重写或丢弃。 |
| 3 | 没有 bridgeif / multi-netif 差异 | Bouffalo 用 lwIP `bridgeif` 把 STA + softAP 桥到同一网段（BL616 主推路由场景）。FreeRTOS+TCP V4.x 才有 multi-endpoint，**没有 bridge**。softAP / repeater 功能直接破，AT `AT+CWMODE=2/3` 路径全废。 |
| 4 | IPv6 成熟度差距 | lwIP 完整 IPv6（`dhcp6/nd6/mld6/ip6_frag/ethip6`）在 SDK 里全开；FreeRTOS+TCP V4 起才有 IPv6，ND/MLD/DHCP6 落后多年。OpenThread Border Router 直接不能用。 |
| 5 | `bouffalo lp change` 12 个内核 patch 没法平移 | 这些 patch 操作 lwIP 内核 TCP keepalive / ARP timer / timer 在 WiFi PS+DTIM 下的行为；FreeRTOS+TCP 的 TCP/timer 实现完全不同，**等效语义要在新栈上重做**，逆向 + 移植成本未知。WiFi 低功耗场景必回归。 |
| 6 | lwIP raw API 用户广泛 | SDK 内 `udp.c` raw API（DHCP server `dhcp_server_raw.c`、mDNS、SNTP、lwiperf）直接用 `udp_new/udp_bind/udp_sendto`。FreeRTOS+TCP **只有 socket API**，没有 raw callback API。 |

### 驱动层 .a 的 nm 扫描（关键发现）

驱动 .a 里**完全没有 ABI 绑定 lwIP**——这跟应用层耦合是两回事。

| 预编译库 | 与 lwIP/网络栈相关的 undef 符号 | 性质 |
|---|---|---|
| `libwl80211_bl616.a` | `wl80211_lwip_init`、`wl80211_tcpip_input` | **2 个 adapter 钩子** |
| `libmacsw_bl616_default.a` | **无** | 干净 |
| `libfhost_bl616_default.a` (552 undef) | **无 lwIP/pbuf/netif/tcpip/sys_arch**；只引用 `net_al_*` / `net_buf_tx_*` adapter | **抽象层调用** |
| `liblpfw_private.a` (低功耗 FW) | **无** | 干净 |
| `libfirmware.a` (wifi4/BL602) | `tcpip_stack_input` | **1 个 adapter 钩子** |
| `libpka_*.a` / `libbl616_phyrf_*.a` | **无** | 与栈无关 |
| `libblecontroller_*.a` / `libbtblecontroller_*.a` | **无 lwIP** | 与栈无关 |

**全部 87 个 .a 加起来，对网络栈的 ABI 依赖收敛到 3 组 adapter 符号**：
- `wl80211_lwip_init` / `wl80211_tcpip_input`（wl80211 ↔ stack）
- `tcpip_stack_input`（wifi4/BL602 ↔ stack）
- fhost 通过 `net_al_*` 函数组（~20 个函数）↔ stack

这 3 组 adapter 的实现都在源码里：
```
components/wireless/wl80211/lwip.c          ← lwIP 版 adapter（260 行）
components/wireless/wl80211/nuttx.c         ← NuttX 版 adapter（750 行，已存在）
components/wireless/wifi4/manager/bl60x_wifi_driver/bl_utils.c   ← BL602 lwIP adapter
components/wireless/wifi6/wifi6_lwip_adapter/net_al.c            ← WiFi6 lwIP adapter (~2000 行)
```

**bouffalo 已经做过 wl80211 的 dual-stack 适配**（`lwip.c` + `nuttx.c` 并存）——这是设计内的，不是历史残留。证明 driver 库的 ABI 边界是可拆的，**第三份 `freertos_tcp.c` 在工程上可写**。

### 工作量量级

| 模块 | 工作量级 | 说明 |
|---|---|---|
| WiFi6 适配层 buffer 翻译 | **大重写** ~2000 行 | pbuf chain ↔ NetworkBufferDescriptor 转换 + DMA 模型 |
| altcp_tls 用户切换到 Transport Interface | **大重写** ~2000-3000 行 SDK | 涉及 HTTPS/MQTT/HTTP client 重做 |
| AT 命令网络栈适配 | **中重写** ~10 个 .c | AT+CIP* / AT+HTTP / AT+MQTT |
| DHCP server / mDNS / SNTP / iperf | **重写** | 从 FreeRTOS+TCP 生态借或自己写 |
| `bouffalo lp change` 等效低功耗 patch | **未知** | 逆向 + 在新栈上重做 |
| bridgeif (STA+AP 桥) | **放弃 or 自写** | FreeRTOS+TCP 无此能力 |
| IPv6 + OpenThread Border Router | **放弃** | FreeRTOS+TCP IPv6 不够成熟 |
| WiFi PS 回归测试 | **3 个芯片全测** | DTIM / sleep current / TCP keepalive |
| 我们自己的 `src/main.cpp` | **2 行改动** | `tcpip_init` → `FreeRTOS_IPInit_Multi` |

### 为什么主流 WiFi SoC 都用 lwIP

ESP32、Realtek（Ameba 全系）、Bouffalo、Cypress、NXP、Nordic NRF7002 的 WiFi SoC 默认 SDK **全部** lwIP。FreeRTOS+TCP 主要场景在以太网 MCU（STM32 + RMII PHY、AWS Reference Integrations），WiFi 上的厂商案例几乎为零。原因就是 pbuf 模型 + altcp + multi-netif/softAP 这三条——FreeRTOS+TCP 不是为 WiFi 复杂多接口场景设计的。

### 结论：★★★★ 不建议

建议替代方案：

| 你的动因 | 更轻量的替代 |
|---|---|
| 想 CVE 跟踪 / 版本统一 | 抽 patch 归档（第一节），不换栈 |
| 想要 coreMQTT/coreHTTP 那套 AWS 生态 | 在 lwIP 上跑 coreMQTT（用 lwIP socket 作为 Transport Interface 后端），不动 TCP/IP 栈 |
| 想要更小内存 | 关 lwIP 的 IPv6/PPP/SNMP/SMTP 等可选模块，比换栈省力 |
| 想要 MISRA / 认证 | lwIP 本身有 SAFE-T 衍生分支；FreeRTOS+TCP 在 WiFi 场景不会比 lwIP 更易认证（还是要审计 ~2000 行新写的 buffer 翻译代码） |

---

## 三、driver-only 实验分支验证

为验证 driver 层与应用栈的解耦命题，建了 `experiment/driver-only` 分支做激进裁剪。

### 实施

分支：`platform/bouffalo_sdk` 内 `experiment/driver-only`（commit `61a2ee0a9`）

**改动规模**：13650 个文件，**4,849,377 行净删**，仅 1 行新增

**删除**（应用层）：
- `components/{ai, crypto, fota, fs, graphics, ipc, iperf, lzma, multimedia, net, usb, wireless}`
- `examples/*`（仅保留 `helloworld`）

**保留**（驱动 + 外设 + 必需基础设施）：
- `drivers/`（LHAL + SoC）、`bsp/`、`cmake/`、`tools/`、`docs/`
- `components/{libc, sysinit, os, debug, shell, utils, mm}`

**迭代过程**：1 次失败 → 1 次通过。唯一坑是 `components/mm/`（TLSF heap，被 newlib `port_memory.c` 和 `bsp/board/*/board.c` 包含 `mm.h`），先删后从 `at-v2.3.26` checkout 回来。

### 结果

| 项目 | 数值 |
|---|---|
| 仓库总大小 | 494 MB（含 `.git`、`docs/`、29 个驱动 `.a`） |
| 代码文件总数 | **1337** 个（.c/.h/.cpp/.S） |
| 其中 `.c` | 631 |
| 其中 `.h` | 684 |
| 代码总行数（.c/.h） | **694,333** 行 |
| BL602 / BL616 / BL616CL helloworld | **全部 build 通过** |

按目录：

| 目录 | 文件数 |
|---|---|
| `drivers/` (LHAL + SoC + PHY/RF) | **686** |
| `bsp/` (board support，6 颗芯片的 board) | **293** |
| `components/` (剩余 7 个基础设施) | **334** |
| `tools/` | 23 |
| `examples/helloworld/` | 1 |

`components/` 细分：

| 子目录 | 文件数 | 作用 |
|---|---|---|
| `libc/` | 129 | newlib + nuttx libc port |
| `os/` | 104 | FreeRTOS 内核 |
| `utils/` | 77 | 基础工具（list/log/math/crc/base64） |
| `mm/` | 10 | TLSF / heap5 堆管理（被 newlib 与 board.c 依赖） |
| `debug/` | 6 | backtrace |
| `shell/` | 4 | letter shell |
| `sysinit/` | 2 | 初始化框架 |

预编译资产：
- **29 个驱动 `.a`** 全部在 `drivers/` 下（`libpka_*` × 10 颗芯片、`lib*_phyrf*` × 6 颗芯片、`libmultimedia_*` 等）
- **0 个 wireless `.a`**（libmacsw/libfhost/libwl80211/libbtblecontroller/libblecontroller/lpfw_private 全部随 `components/wireless/` 删除）

### 含义

删除 lwIP/mbedTLS/WiFi/BT/USB/FS 后**驱动层零编译错误** → 印证 nm 数据：driver `.a` 不绑 lwIP，bouffalo_sdk 的 driver/peripheral 层与应用栈是干净解耦的。

`components/mm`（heap manager）是必需的边界，应归属"基础设施"而非"应用层"。

如果后续要做 FreeRTOS+TCP POC，**这个分支正好是干净起点**：在它之上加回 `wireless/` 的 driver `.a` + 写一份 `wl80211/freertos_tcp.c` adapter 即可。

---

## 四、三条 WiFi driver 路线

来自 Bouffalo / 博流社群确认（陈磊、夏一铭、汪、荣卓然）+ SDK 代码验证：

| 路线 | 出处 | 当前默认芯片 | 我们 SDK 里 adapter | 内存 | 状态 |
|---|---|---|---|---|---|
| **fhost** | RivieraWaves（CEVA RW 系，被 Renesas 收购） | BL616 (WiFi 6 主线) | `wifi6_lwip_adapter/net_al.c` ~2000 行 | 大 | 成熟、起步早，低功耗主用 |
| **wl80211** | Bouffalo 自研（结构借 FreeBSD net80211） | BL616CL / BL618DG（也支持 BL616） | `wl80211/{lwip.c, nuttx.c}`，**已 dual-stack** | 小 | 用时间短，缺功能时按需补 |
| **net80211** | 计划中（ESP/乐鑫走这条） | 未发布 | — | 未知 | Bouffalo 后续会出 |

### BL616 也支持 wl80211（不只是 BL616CL）

证据：

1. **预编译库存在**：
```
components/wireless/wl80211/lib/
├── libwl80211_bl616.a    ← BL616 wl80211 driver
├── libwl80211_bl616cl.a
└── libwl80211_bl618dg.a
```
`wl80211/CMakeLists.txt:19` 按 `${CHIP}` 选 `libwl80211_${CHIP}.a`，bl616 路径开通。

2. **现成的 BL616 + wl80211 示例**：`examples/wifi/macsw_bare/defconfig`：
```
#CONFIG_FHOST            =y
CONFIG_WL80211           =y
CONFIG_WL80211_WRAM_MEM_SIZE_LIMIT = 0x10000  # 64KB
CONFIG_WIFI6             =y
```

3. **Bouffalo SDK 自己写明了"惯例 vs 能力"**：`examples/pmu/wl_lp/defconfig` 顶部注释：
```
# WiFi stack selection: BL616 uses fhost; BL616CL/BL618DG use wl80211
```
这是默认配置惯例，不是硬件限制。

### 选型权衡

| 维度 | fhost (RW) | wl80211 (Bouffalo) |
|---|---|---|
| 内存占用 | 大 | 小 (64 KB wram limit 内可跑) |
| WiFi6 吞吐 | 高 | 较低 |
| 成熟度 | 高（起步早） | 中（缺功能可问 Bouffalo 补） |
| 多 stack 适配 | 单 lwIP 绑定 | dual-stack 设计 (`lwip.c` + `nuttx.c`) |
| 换栈难度 | 高（重写 net_al.c ~2000 行） | **低（抄 nuttx.c 改一份 freertos_tcp.c）** |

---

## 五、POC 路径建议

如果决定做 FreeRTOS+TCP 验证，**最低成本路径**：

### 阶段 1：在 BL616 上从 fhost 切到 wl80211（不动 SDK 源码）

把 `CONFIG_WL80211=y / CONFIG_FHOST=n` 套到 BL616，验证 wl80211 路径在我们的硬件 + board + build pipeline 上能跑通。
- 完全在 Bouffalo 自带能力内
- 失功能：WiFi6 高吞吐被换成 wl80211 路径（更省内存，吞吐低）
- 工作量：1-2 周
- 输出：BL616 + wl80211 + lwIP 跑通 STA + DHCP + ping + TCP socket

### 阶段 2：写 `wl80211/freertos_tcp.c`

抄 `wl80211/nuttx.c`（750 行）写一份 FreeRTOS-Plus-TCP adapter，替换 `wl80211/lwip.c`。
- 工作量：1-2 人月
- 风险点：buffer 翻译性能、WiFi PS 下 TCP keepalive 行为
- 这两个数据出来后才能决定是否做全量 SDK 应用层迁移（altcp_tls 用户、AT 命令、HTTP/MQTT、DHCPD 等，3-6 人月）

### 阶段 3（不必做）：fhost 路径换栈

只在确认 wl80211 路径不够用、必须保留 WiFi6 高吞吐时才做。重写 `wifi6_lwip_adapter/net_al.c` ~2000 行。

### 等待 net80211（可能更划算的选项）

Bouffalo 计划出 net80211 版本。等其落地后，可能比写 FreeRTOS+TCP adapter 更划算——同样是换驱动栈，net80211 是厂商支持的方向，跟着乐鑫生态走。

---

## 附：相关文件位置（在 bouffalo_sdk submodule 内）

- WiFi driver adapter：
  - `components/wireless/wl80211/{lwip.c, nuttx.c}` — wl80211 dual-stack adapter
  - `components/wireless/wifi6/wifi6_lwip_adapter/net_al.c` — WiFi6 fhost lwIP adapter
  - `components/wireless/wifi4/manager/bl60x_wifi_driver/bl_utils.c` — BL602 wifi4 lwIP adapter
- lwIP：
  - `components/net/lwip/lwip/src/` — upstream 2.1.2 + 12 个 bouffalo lp change patch
  - `components/wireless/wifi6/wifi6_lwip_adapter/include/lwipopts.h` — 实际生效 lwipopts
- 默认配置参考：
  - `examples/wifi/macsw_bare/defconfig` — BL616 + wl80211 + lwIP 参考
  - `examples/pmu/wl_lp/defconfig` — 多芯片 fhost/wl80211 选择注释
- 实验分支：
  - `platform/bouffalo_sdk` 内 `experiment/driver-only`（commit `61a2ee0a9`）
