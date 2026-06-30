# Realtek Ameba 网络栈方案评估

针对 ameba-rtos / ameba-rtos-d 两套 Realtek SDK 做同样的网络栈调研：lwIP 抽出、FreeRTOS+TCP 替换、driver `.a` 的栈绑定情况。和 Bouffalo 阵营（[bouffalo_network_stack_eval.md](bouffalo_network_stack_eval.md)）对比。

## TL;DR

- **新老 SDK 设计完全不同**。AmebaDplus/Green2（`ameba-rtos`）做了一次干净的重构：driver `.a` 不再直接绑 lwIP upstream API，改走 `LwIP_*` 封装层；AmebaD（`ameba-rtos-d`）老 SDK driver `.a` **直接引用 `pbuf_alloc/netif_add/tcpip_init/tcpip_input`** + Realtek 私有 lwIP API（`netif_pre/post_sleep_processing`），ABI 绑死 patched lwIP。
- **lwIP 私有 patch 量是 Bouffalo 的 4-5 倍**：ameba-rtos lwip_v2.1.2 **54/291 文件改动 (~19%)**，远高于 Bouffalo 的 12/295 (~4%)。patch 类型是产品级 TCP 状态机修改 + `CONFIG_STANDARD_TICKLESS` 低功耗钩子 + `LWIP_HOOK_TCP_ISN` 安全增强，**不能丢**。
- **没有 dual-stack adapter**。`lwip_netconf.h/c` 是 thin wrapper（~900 行），但只有 lwIP 一份实现，没有 Bouffalo `wl80211/nuttx.c` 那种第二份 alt-stack 适配证据。换栈是从 0 写第二份。
- **意外好消息**：Realtek 整个阵营 **0 个 `altcp_tls` 用户、0 个 lwIP raw udp/tcp pcb 外部用户**。换 FreeRTOS+TCP 时不用重写 HTTPS/MQTT 客户端的 altcp 路径——这是 Bouffalo 阵营的痛点之一，Realtek 没有。
- **换栈难度排序**（含 Bouffalo 对比）：
  - Bouffalo wl80211 ★★（dual-stack 设计内）
  - Bouffalo fhost ★★★★（net_al.c 2000 行 + altcp_tls 重写）
  - **Realtek AmebaDplus/Green2 ★★★★**（封装层在 source code 里可改，但 54 个 lwIP 内核 patch 语义难复刻）
  - **Realtek AmebaD ★★★★★ 不建议**（.a 直接绑 lwIP upstream + 私有 patched API + 双 lwIP 版本 v2.0.2/v2.2.0 共存）

---

## 一、Driver `.a` 的 lwIP 绑定（nm 实证）

工具：`arm-none-eabi-nm`（路径在 `platform/ameba-rtos-d/.../toolchain/.../bin/`）。

### 新 SDK (ameba-rtos / AmebaDplus / AmebaGreen2)

| 库 | lwIP 相关 undef 符号 | 性质 |
|---|---|---|
| `lib_wifi_common.a` | **0** | 完全干净 |
| `lib_wifi_rtk_app.a` | **0** | 完全干净 |
| `lib_wifi_whc_dev.a` | 9 个：`whc_dev_get_lwip_info`、`netif_rx`、`whc_dev_set_netif_info`、`whc_dev_wtn_socket_*`、`wifi_if_netif_rx` 等 | **抽象层调用**，无 lwIP upstream API |
| `lib_wifi_whc_dev_tcpip.a` | 21 个：`LwIP_GetIP/GetGW/GetMAC/GetMASK`、`LwIP_netif_set_up/down/link_up/link_down`、`LwIP_netif_is_valid_IP`、`LwIP_wlan_set_netif_info` + `netif_rx`、`netif_is_valid_IP` | **Realtek 自封装层**（`LwIP_*` 前缀），实现在 `lwip_netconf.c`/`lwip_host.c` source 文件里 |
| `lib_wifi_whc_dev_wpaoffload.a` | 1 个：`netif_rx` | Realtek 自定义回调（非 lwIP upstream） |
| `lib_wifi_whc_ap.a` | 12 个：`LwIP_*` 系列 + 1 个 `pbuf_alloc`（lwIP upstream） | 几乎全是封装层，只 1 个 raw API |

**关键认知**：
- 新 SDK driver `.a` **不直接调用 lwIP upstream API**（除 1 处 `pbuf_alloc`、若干 `netif_rx`）。
- 抽象通过 `LwIP_*` 前缀函数实现，源码在 `component/lwip/api/lwip_netconf.c`（719 行）和 `component/wifi/whc/whc_host_stm32/lwip_host.c`（498 行）。
- 换栈时改这两个文件即可，driver `.a` 不需要重编。

### 老 SDK (ameba-rtos-d / AmebaD)

| 库 | lwIP 相关 undef 符号 |
|---|---|
| `lib_wlan.a` / `lib_wlan_mp.a` / `lib_wlan_lp.a` | 12 个：**`netif_add`、`netif_set_default`、`netif_set_up`、`pbuf_alloc`、`pbuf_free`、`tcpip_init`、`tcpip_input`**（lwIP upstream raw API） + `netif_pre_sleep_processing`、`netif_post_sleep_processing`（**Realtek 私有 lwIP patch API**） + `LwIP_GetIP` 封装 |

**关键认知**：
- 老 SDK driver `.a` **直接调用 lwIP upstream raw API**，ABI 绑死 lwIP 2.0/2.2 系。
- 还引用了 Realtek 私有 lwIP patch API（`netif_pre_sleep_processing` 等）—— 这意味着 driver `.a` **编译时基于 patched lwIP**。
- 换栈 = 必须复刻 lwIP 公共 API + Realtek 私有 API 的同名签名，否则 driver `.a` 不能链接。

---

## 二、lwIP 私有 patch 量

| SDK | 版本 | 改动 / 总文件 | 比例 |
|---|---|---|---|
| Bouffalo | 2.1.2 | 12/295 | **~4%** |
| Realtek **ameba-rtos** | 2.1.2 | **54/291** | **~19%** |
| Realtek ameba-rtos-d | 2.0.2 | 28/245 | ~11% |
| Realtek ameba-rtos-d | 2.2.0 | 21/298 | ~7% |

### Realtek 私有 patch 类型（ameba-rtos lwip_v2.1.2 tcp.c 抽样）

| 区域 | 内容 |
|---|---|
| `tcp.c:121-130` | `LWIP_HOOK_TCP_ISN` hook + `CONFIG_STANDARD_TICKLESS` 低功耗 sleep 钩子，引用 `ameba_soc.h` 和 `SLEEP_ParamDef` |
| `tcp.c:1048-1065` | TCP 状态机修改（Realtek add 块） |
| `tcp.c:1361` | `FIN_WAIT_1` 状态判断额外条件 |
| `tcp.c:2262-2278` | TCP ISN（Initial Sequence Number）安全增强：用硬件随机源 `LWIP_RAND` 重做 ISN |
| `tcp.c:2735-2815` | Realtek add 块 |

`tcp.c` 单一文件就有 **9 处** Realtek 标记，散布在 TCP 状态机、ISN、keepalive、状态变换逻辑里——**全是产品级 patch，不能丢**。

`netif.h` 还加了 `netif_pre_sleep_processing/netif_post_sleep_processing` Realtek 私有 API（被 driver `.a` 引用，所以这两个函数签名必须保留）。

---

## 三、Stack 适配层结构

### 新 SDK 抽象（ameba-rtos）

```
component/lwip/api/lwip_netconf.{h,c}          ← 719 行，提供 LwIP_* 封装层
component/wifi/whc/whc_host_stm32/lwip_host.c  ← 498 行，host 侧封装 (SPI/SDIO/UART trx)
component/wifi/whc/whc_dev/whc_dev_app.c       ← device 侧 app，调 LwIP_netif_set_link_up
component/wifi/driver/intf/wifi_intf_drv_to_lwip.h ← driver↔stack interface header
component/lwip/lwip_v2.1.2/port/realtek/freertos/ethernetif.c  ← 直接 #include "wifi_api.h"、调 rltk_wlan_send/recv
```

`lwip_netconf.h` 公开的封装 API（22 个 `LwIP_*` 函数）：
```c
void     LwIP_Init(void);
void     LwIP_ReleaseIP(uint8_t idx);
uint8_t  LwIP_DHCP(uint8_t idx, uint8_t dhcp_state);
void     LwIP_DHCP_stop(uint8_t idx);
void     LwIP_netif_set_up/_down(uint8_t idx);
void     LwIP_netif_set_link_up/_down(uint8_t idx);
void     LwIP_wlan_set_netif_info(uint8_t idx, void *dev, unsigned char *dev_addr);
void     LwIP_ethernetif_recv(uint8_t idx, int total_len);
void     LwIP_ethernetif_recv_inic(uint8_t idx, struct pbuf *p_buf);
int      LwIP_netif_is_valid_IP(uint8_t idx, unsigned char *ip_dest);
void     LwIP_GetDNS/SetDNS(struct ip_addr *dns);
void     LwIP_SetIP(uint8_t idx, u32_t addr, u32_t netmask, u32_t gw);
void     LwIP_AUTOIP[_STOP|_IPv6](uint8_t idx);
int      LwIP_netif_get_idx(struct netif *pnetif);
int      LwIP_Check_Connectivity(uint8_t idx);
uint8_t  LwIP_IP_Address_Request(uint8_t idx);
uint8_t *LwIP_GetIP/GetGW/GetMAC/GetMASK(uint8_t idx);
```

这套封装比 Bouffalo wl80211 `lwip.c`（260 行）更厚（720 行），**API 体积更大**。

### 老 SDK 抽象（ameba-rtos-d）

```
component/common/api/lwip_netconf.{h,c}                      ← 同名同结构
component/common/network/lwip/lwip_v2.0.2/port/realtek/freertos/ethernetif.c
component/common/network/lwip/lwip_v2.2.0/port/realtek/freertos/ethernetif.c  ← 双版本并存
```

`ethernetif.c` 直接 `#include "lwip/pbuf.h"`、`"lwip/tcpip.h"` 等 lwIP 头，调 `rltk_wlan_send/recv`：

```c
// AmebaD lwip_v2.0.2/port/realtek/freertos/ethernetif.c
#include "lwip/opt.h"
#include "lwip/pbuf.h"
#include "lwip/tcpip.h"
#include "lwip/ethip6.h"
#include <lwip_intf.h>
...
rltk_wlan_send(netif_get_idx(netif), sg_list, sg_len, p->tot_len);
rltk_wlan_recv(netif_get_idx(netif), sg_list, sg_len);
```

这一层耦合在两边都存在，但老 SDK driver `.a` 还往 lwIP upstream API 里调（pbuf_alloc/tcpip_input），**比新 SDK 紧得多**。

### 与 Bouffalo 的关键差异

| 维度 | Bouffalo wl80211 | Realtek ameba-rtos |
|---|---|---|
| Driver↔stack adapter 文件数 | 2 (`lwip.c` + `nuttx.c`) | 1 (`lwip_netconf.c`) |
| Dual-stack 设计证据 | **有**（已实装 NuttX 版本） | **无**（只 lwIP 一份） |
| 封装层 API 数量 | ~10 (lwip.c 260 行) | ~22 (lwip_netconf.h 公开声明) |
| Stack-coupled 适配文件 | `wl80211/lwip.c` 单点 | `ethernetif.c` + `lwip_netconf.c` + `lwip_host.c` 三处 |

Realtek 封装层比 Bouffalo 设计得"更完整"，但缺少 dual-stack 适配证据——换栈时是从 0 写第二份，不像 Bouffalo 有 `nuttx.c` 现成模板可抄。

---

## 四、应用层 lwIP 耦合

| 调查项 | Bouffalo | Realtek ameba-rtos |
|---|---|---|
| 外部 `#include "lwip/"` 文件数 | ~74 | **28**（去 `/lwip/lwip_v2.1.2/src/` 后） |
| `altcp_tls` 用户 | 重（HTTPS/MQTT/HTTP/FOTA client + AT 模块） | **0** |
| `lwip/altcp_*` 任何用户 | 多 | **0** |
| lwIP raw API (`udp_new/tcp_pcb`) 外部用户 | 多 | **0** |

### Realtek 28 个 lwIP 外部用户分布

```
component/at_cmd/                          (AT 命令模块)
component/wifi/rtk_app/wifi_tunnel_app/    (WiFi tunnel app)
component/wifi/whc/whc_host_stm32/         (STM32 host trx)
component/wifi/whc/whc_host_rtos/          (RTOS host)
component/wifi/whc/whc_dev/                (device 侧)
component/wifi/whc/whc_host_threadx_fullhan/ (ThreadX fullmac)
component/lwip/api/                        (lwip_netconf 自身)
component/ssl/mbedtls-3.6.2/library/       (mbedTLS 自己也 include 一些 lwIP？)
component/ssl/mbedtls-3.6.5/library/
component/audio/cmds/pcrecord/             (音频录制)
component/dynamic_app/                     (动态 app)
```

**意外发现**：Realtek SDK **完全不用 `altcp_tls`**。HTTPS / MQTT / TLS 客户端走原始 socket + mbedTLS 直接对接的路径，不经过 lwIP 的 TLS shim。

这是和 Bouffalo 阵营最大的实战差异：换 FreeRTOS+TCP 时 Bouffalo 要重写 ~2000-3000 行 altcp 用户代码，**Realtek 这部分工作量是 0**。

---

## 五、AmebaD 双 lwIP 版本共存

`ameba-rtos-d/component/common/network/lwip/` 下：
- `lwip_v2.0.2/` —— init.h 显示 2.0.3，目录名 2.0.2，**28/245 文件改动**
- `lwip_v2.2.0/` —— **21/298 文件改动**

为什么并存？历史路径：
- v2.0.2 是 AmebaD 出厂版本，旧代码（旧版 linkkit / aliyun_iot / matter）绑死它的 API
- v2.2.0 是后来 bump 上来的，新代码用它

`ethernetif.c` 两个版本各一份，私有 patch 各自维护。

**含义**：如果对 AmebaD 做换栈，要先决定走哪个版本——v2.0.2 + 28 patch 还是 v2.2.0 + 21 patch。两个都不能丢的话工作量翻倍。

---

## 六、换 FreeRTOS+TCP 的工作量分析

### Realtek AmebaDplus/Green2（新 SDK）

| 模块 | 工作量级 | 说明 |
|---|---|---|
| 重写 `lwip_netconf.c` 适配新栈 | **中重写** ~720 行 | 实现 22 个 `LwIP_*` 封装函数的 FreeRTOS+TCP 版本 |
| 重写 `lwip_host.c` | **中重写** ~500 行 | host trx 侧的 stack 接入 |
| 重写 `port/realtek/freertos/ethernetif.c` | **大重写** | pbuf chain → NetworkBufferDescriptor 翻译，配合 `rltk_wlan_send/recv` |
| 54 个 lwIP 内核 patch 等效语义 | **未知** | TCP 状态机、ISN hook、tickless sleep 钩子要在新栈实现 |
| altcp_tls 用户改造 | **0** | 没有 altcp_tls 用户 |
| AT 命令网络栈适配 | **中** | `component/at_cmd/atcmd_sockets.c` 等 |
| WiFi PS 回归测试 | **3 颗芯片** | AmebaDplus / Green2 / Smart |
| 我们的 `src/main.cpp` Realtek 分支 | **2-3 行** | `tcpip_init` → `FreeRTOS_IPInit_Multi` |

**估算**：3-6 人月（比 Bouffalo 同等级，但少了 altcp 重写、多了 patch 复刻）

### Realtek AmebaD（老 SDK）

| 阻碍点 | 性质 |
|---|---|
| driver `.a` 直接绑 `netif_add/pbuf_alloc/tcpip_init/tcpip_input` | **必须保留 lwIP 同名 API**——否则 `lib_wlan.a` 不能链接 |
| driver `.a` 引用 `netif_pre_sleep_processing` 等 Realtek 私有 lwIP API | **必须实现同签名**，且行为等效 |
| 双 lwIP 版本（v2.0.2 + v2.2.0） | 工作量 ×2 |

**结论**：AmebaD 不建议换。哪怕做了 shim 层去满足 lwIP 公共 API 名字 + 私有 API 名字，行为差异会带来 driver 内部 timing 问题，回归成本难以预估。

---

## 七、和 Bouffalo 对比的综合表

| 维度 | Bouffalo | Realtek AmebaDplus/Green2 | Realtek AmebaD |
|---|---|---|---|
| Driver `.a` 是否绑 lwIP upstream | ❌ adapter 钩子 | ❌ 走 `LwIP_*` 封装 | ✅ **直接绑 raw API** |
| lwIP 私有 patch 量 | 12/295 (~4%) | **54/291 (~19%)** | 28+21 双版本 |
| 私有 patch 类型 | 低功耗 (PS/DTIM 配套) | TCP 状态机 + ISN + tickless sleep | 类似 + 私有 sleep API |
| Dual-stack adapter 设计证据 | ✅ wl80211 已有 nuttx.c | ❌ 无 | ❌ 无 |
| `altcp_tls` 用户数 | 多 | **0** | **0** |
| lwIP 版本数 | 1 | 1 | **2** (并存) |
| 适配文件总行数 | wl80211 ~1000 行 + net_al ~2000 行 | lwip_netconf ~720 + lwip_host ~500 + ethernetif ~600 ≈ 1820 行 | 类似 + 双份 |
| 换 FreeRTOS+TCP 难度 | wl80211 ★★ / fhost ★★★★ | **★★★★** | **★★★★★** |
| 推荐换栈路径 | wl80211（抄 nuttx.c） | 改 `lwip_netconf.c` + ethernetif | 不建议 |

---

## 八、抽出 Realtek lwIP 单独维护？

跟 Bouffalo 那边的评估一样，是另一个问题——三家 SDK 共抽 lwIP 已经在 [shared_components_eval.md](shared_components_eval.md) 评估过，结论是 ❌ 不建议。

Realtek 单家抽出来：
- 54 个改动文件 / 19% 比例**比 Bouffalo 高 5 倍**——patch 化工作量大
- 双 lwIP 版本（AmebaD）抽之前要先并版本——但 v2.0.2 / v2.2.0 各自服务不同历史代码，并版本本身就是大工程
- 长期维护成本高于收益

建议同 Bouffalo：先做 patch 归档（`at-project/patches/realtek_lwip_2.1.2/`），物理不移动，等三家共抽时再统一动手。

---

## 九、POC 路径建议

如果未来要在 Realtek 阵营做 FreeRTOS+TCP 验证：

### 阶段 1：选 AmebaDplus 作 POC 起点

- 新 SDK 路径
- driver `.a` 不绑 lwIP upstream
- 单 lwIP 版本（v2.1.2）
- 0 altcp_tls 用户

### 阶段 2：实施

1. 把 `LwIP_*` 22 个封装函数在 `lwip_netconf_freertos_tcp.c` 重做一份（参照原 `lwip_netconf.c`）。
2. 把 `port/realtek/freertos/ethernetif.c` 改成 FreeRTOS+TCP 风格的 `NetworkBufferDescriptor_t` 输入输出，对接 `rltk_wlan_send/recv`。
3. 把 54 个 lwIP 内核 patch 中关键的 5-10 个（TCP ISN、tickless sleep 钩子）在 FreeRTOS+TCP 上重做。其余非关键 patch 可暂时丢。
4. 跑 STA-only DHCP + ping + 单 TCP socket，对比 lwIP 路径的性能 / PS 行为。

工作量预估：2-3 人月（无 altcp_tls 是大节省）。

### 阶段 3（不建议）：AmebaD

driver `.a` ABI 绑死 lwIP upstream。即使做 shim 层骗 `.a` 链接，行为差异不可控。**建议跳过**。

---

## 附：相关文件位置

- WiFi driver 抽象：
  - `platform/ameba-rtos/component/wifi/whc/whc_host_stm32/lwip_host.c` (498 行)
  - `platform/ameba-rtos/component/wifi/whc/whc_dev/whc_dev_app.c`
  - `platform/ameba-rtos/component/wifi/driver/intf/wifi_intf_drv_to_lwip.h`
- lwIP 封装层：
  - `platform/ameba-rtos/component/lwip/api/lwip_netconf.{h,c}` (176/719 行)
  - `platform/ameba-rtos-d/component/common/api/lwip_netconf.{h,c}`
- Stack port：
  - `platform/ameba-rtos/component/lwip/lwip_v2.1.2/port/realtek/freertos/ethernetif.c`
  - `platform/ameba-rtos-d/component/common/network/lwip/lwip_v2.{0.2,2.0}/port/realtek/freertos/ethernetif.c`
- 私有 patch 抽样：
  - `platform/ameba-rtos/component/lwip/lwip_v2.1.2/src/core/tcp.c` 第 121, 1048, 1361, 2262, 2735 行
- 关键 driver `.a`：
  - 新 SDK 干净边界：`platform/ameba-rtos/component/soc/amebadplus/project/project_km4/lib/application/lib_wifi_whc_dev{,_tcpip}.a`
  - 老 SDK 绑 lwIP：`platform/ameba-rtos-d/project/realtek_amebaD_va0_example/GCC-RELEASE/project_hp/asdk/lib/application/lib_wlan{,_mp,_lp}.a`
