# 共用组件分离评估：FreeRTOS / lwIP / mbedTLS

## TL;DR
- **FreeRTOS**：🟡 可以抽（用 v10.6.2 做共用基线，覆盖三家），但 AmebaGreen2 把 FreeRTOS 烧进 ROM（`component/os/freertos/freertos_rom/amebagreen2/freertos_v10.4.3/`），那颗片子永远受限于 10.4.3 ABI。
- **lwIP**：❌ 不建议短期内分离。Ameba 在 lwIP 核心改了 56/291 个文件、Bouffalo 12/295，私有 patch 互相不兼容；硬抽要先做 patch 重整。
- **mbedTLS**：✅ 建议优先做。upstream library/ 几乎零改动（grep "Realtek/Bouffalo" 在 ssl_tls.c 都是 0 命中），所有 HW 加速都走 `MBEDTLS_*_ALT` callback，抽出一份 3.6.x 共用基线最干净。

## 评估方法 + 数据采集
- 直接对三家 SDK 的组件目录做 `find` / `wc -l` / `grep` 找改动注释（`Realtek add`、`/* Realtek`、`Added by Realtek`、`bouffalo lp change` 等是芯片厂的统一标记）。
- 核心文件取 FreeRTOS `tasks.c`、lwIP `src/core/tcp.c`、mbedTLS `library/ssl_tls.c`。
- 私有改动量统计：「文件级 grep 命中 Realtek/bouffalo 标记的源文件数 / 全部源文件数」。
- 抽象边界：portable/`MBEDTLS_*_ALT`/lwIP `port/` 是否够干净到不必碰 upstream。

---

## FreeRTOS

### 三个 SDK 的现状

| SDK | 版本 | 路径 | tasks.c 行数 | 私有改动痕迹 |
|---|---|---|---|---|
| bouffalo_sdk | 10.6.2 | `platform/bouffalo_sdk/components/os/freertos/` | 5745 | tasks/queue/timers/event_groups/list 全部 `grep Bouffalo`=0；`portable/GCC/RISC-V/c906/port.c` 是私有 RISC-V port（c906/common/RISC-V 共 744 行）|
| ameba-rtos-d | 10.2.0 | `platform/ameba-rtos-d/component/os/freertos/freertos_v10.2.0/Source/` | 5265 | `timers.c` 有 2 处 "Added by Realtek to prevent timer thread blocked"；tasks.c 没有「Realtek add」注释；额外 `freertos_service.c`(1117) `cmsis_os.c`(1396) 是 Realtek 的 OS 抽象层 |
| ameba-rtos | 10.4.3 / 10.5.1 / 10.2.1_smp + Green2 **ROM** 副本 | `platform/ameba-rtos/component/os/freertos/freertos_v10.4.3/Source/` | 5184 | tasks.c 顶部 `#include "diag.h"`；3 处 `/* Realtek add */`（`xTaskGetPendedTicks`、`…FromISR`、`xTaskAbortDelay` 周边）；其余 list/queue/timers/event_groups grep "Realtek"=0；另有 `os_wrapper/`(10 个文件 ~1830 行) 做新版 HAL |

### 抽象层分析
- **port 层**：三家完全互斥的硬件 port——Bouffalo 自定义 RISC-V c906/common port，AmebaD 用 ARM_CM4F/ARM_CM3，Ameba 新 SDK 一堆 `AmebaLite_KM4/AmebaSmart_KM4/KM0/AmebaDplus_KM4` 自定义 portable 目录。抽出公共内核以后这部分必须留在各自 SDK，没有改动空间。
- **FreeRTOSConfig.h**：三家分散在各自 chip 目录（AmebaD 246 行、AmebaDplus KM4 263 行、bouffalo examples 各一份），可保留；共用内核读宏。
- **OS 抽象层**：Ameba 系都有自己的 wrapper（`freertos_service.c` / `os_wrapper/`），这部分仍属 SDK 专有，跟内核解耦。

### 风险点
- AmebaGreen2 FreeRTOS 在 ROM (`freertos_rom/amebagreen2/freertos_v10.4.3/`)，内核 ABI 锁死 10.4.3；如果共用基线选 10.6.2，Green2 必须保留 ROM 兼容层（v10.4.3 仍要构建），等于该芯片是「双内核」。
- AmebaD/Ameba 在内核里塞了 `xTaskGetPendedTicks`、`xTaskGetPendedTicksFromISR` 等 Realtek 私有 API，迁到 10.6.2 后这些函数要么手工补回（patch 文件），要么改 caller。
- AmebaD 的 timers.c 加了「prevent timer thread blocked」死循环 fallback——业务代码可能依赖该行为，先做行为差异 review。

### 结论：🟡 可以抽，但有代价
**实施步骤**：
1. 选 10.6.2 做公共 git submodule（mirror FreeRTOS-Kernel V10.6.2 tag）。
2. 抽出 `third_party/freertos-kernel/`，提取 Realtek/Bouffalo 私有补丁为 `patches/*.patch`（重点 tasks.c 3 处、timers.c 2 处、`xTaskGetPendedTicks` 系列 API）。
3. 三个 SDK 的 portable/ 和 FreeRTOSConfig.h 不动，仍随各自 SDK 走。
4. AmebaGreen2 单独保留 `freertos_rom/`，构建时 RAM 部分用 patch 后的 10.6.2，ROM 接口走 SDK 自带 wrapper。
5. 长期目标：把 `xTaskGetPendedTicks` 这类 API 提交回 FreeRTOS 上游，让 patch 收敛到 0。

---

## lwIP

### 三个 SDK 的现状

| SDK | 版本 | 路径 | tcp.c 行数 | 私有改动文件数 |
|---|---|---|---|---|
| bouffalo_sdk | 2.1.2 | `platform/bouffalo_sdk/components/net/lwip/lwip/src/` | 2893 | 12 / 295 (~4%)，标记 "bouffalo lp change"，集中在 `core/tcp.c`、`tcp_in.c`、`tcp_out.c`、`dns.c`、`ipv4/dhcp.c`、`ipv4/etharp.c`、`ipv4/ip4_frag.c`、PPP 那几个 |
| ameba-rtos-d | 2.0.3（目录名 lwip_v2.0.2，init.h 显示 2.0.3）+ 2.2.0 双份 | `platform/ameba-rtos-d/component/common/network/lwip/lwip_v2.2.0/src/` | 2715 | 21 / ~290（2.2.0）+ 28（2.0.3）—— `api/`、`core/`、`netif/`、`include/lwip/` 都改了 |
| ameba-rtos | 2.1.2 | `platform/ameba-rtos/component/lwip/lwip_v2.1.2/src/` | 2817 | **56 / 291 (~19%)**，tcp.c 内有 9 处 `Realtek add / Added by Realtek start/end`（FIN_WAIT_1 处理、PERSIST timer 改动、TCP keepalive 等） |

### 抽象层分析
- **port 层**：三家都有清楚的 `lwip-port/`/`port/realtek/`，包含 `sys_arch.c`/`ethernetif.c`/`lwipopts.h`，Ameba 还多了 `bridgeif.c`/`br_rpt_handle.c`（1571 行的私有桥接转发）。这层属于 driver/MAC 集成，必须留在各 SDK。
- **WiFi 耦合点**：
  - Ameba `lwip_v2.1.2/port/realtek/freertos/ethernetif.c` 直接 `#include "wifi_api.h"` + `<wifi_intf_drv_to_lwip.h>` 调 `rltk_wlan_send/recv`。
  - Bouffalo `lwip-port/FreeRTOS/ethernetif.c` 是一份干净的 lwIP skeleton（grep "wifi/bl_wifi/wireless_mgmr"=0），真正的 WiFi 胶水代码在 `components/wireless/wifi6/wifi6_lwip_adapter/` 独立组件里——架构上更干净。
- **lwipopts.h**：三家各 1 份，互不兼容。

### 风险点
- Ameba 在 lwIP **core** 改了 56 文件，其中很多是 TCP 状态机/重传/keepalive 等关键路径，不是简单 hook——直接换成 upstream 2.1.2 会改变 TCP 行为，wifi/省电场景大概率回归。Realtek 那 56 个文件的改动必须每个走 patch 审计才能进 upstream-line。
- AmebaD 还在维护两份 lwIP（2.0.3 + 2.2.0），先得选定一份。
- Bouffalo 的 "lp change"（低功耗下 TCP 行为修改）是产品特性，不能简单回退。

### 结论：❌ 不建议短期分离
合规收益 ≠ 维护成本。三家在 lwIP 核心的私有 patch 几乎每个都是产品级修改，硬抽要先做 ~80 个 patch 的语义审计，工作量数量级大于 FreeRTOS。

**如果一定要做**（中长期）：
1. 先做「patch 化」：把三家相对 upstream 2.1.2 的 diff 都生成 patch 集合（用脚本对比 upstream 文件）。
2. 评估每个 patch 能否上 upstream（部分 Realtek TCP 修复其实是 lwIP bug，可以贡献回去）。
3. 剩余的产品 patch 收敛到 `patches/<vendor>/` 里，构建时 apply。
4. AmebaD 先升 2.2.0 → 2.1.2，把版本数从 4 个砍到 1 个，再谈共用。

---

## mbedTLS

### 三个 SDK 的现状

| SDK | 版本 | 路径 | ssl_tls.c 行数 | 私有改动 |
|---|---|---|---|---|
| bouffalo_sdk | 2.28.2 (LTS) + **3.6.5** | `…/crypto/mbedtls/mbedtls_v3/library/` | 10187 | `grep Realtek\|Bouffalo`=0；HW 加速放在 `port/hw_acc/`（aes/sha/ecp/sha256 等 8 个 `_alt.c`，共 ~6500 行） |
| ameba-rtos-d | 2.4.0 / 3.2.1 / 3.4.0 / **3.6.0** 四份并存 | `…/network/ssl/mbedtls-3.6.0/library/` | 9959 | `grep Realtek`=1（`ssl_tls.c` 一处）；HW 加速通过 `library_s/` 旁路实现 |
| ameba-rtos | 3.6.2 + **3.6.5** | `…/ssl/mbedtls-3.6.5/library/` | 10187 | `grep Realtek`=0；HW 加速集中在 `hw_alt/`（aes/sha256/sha512/ecc/rsa/entropy 9 个 `_alt.c`），mbedTLS 还做了 ROM split（`library_rom/ecp_rom.c` 3734 行 in ROM）|

### 抽象层分析
- 三家**全部走 `MBEDTLS_*_ALT` 机制**，upstream library/ 基本零改动，证据是核心文件 `ssl_tls.c` 行数一致（10187 = upstream 3.6.5），且 grep "Realtek/Bouffalo" 在 mbedtls_v3/library 和 mbedtls-3.6.5/library 都是 0 命中。
- 配置文件机制各自不同但都允许覆盖：
  - Bouffalo: `MBEDTLS_CONFIG_FILE` → `config-tls-generic.h`，启用 `MBEDTLS_SHA1/256/512/AES/ECP_ALT` + `MBEDTLS_ENTROPY_HARDWARE_ALT`。
  - Ameba: `MBEDTLS_CONFIG_FILE="mbedtls/config.h"`，里面 #include 到 `mbedtls_config_green2.h`/`mbedtls_config_legacy.h`/`mbedtls_config_rtl8720f.h`，分别启用不同 ALT。AmebaGreen2 还启用 `MBEDTLS_ECDH/ECDSA_*_ALT` + 自定义 `RTK_LALU_MBEDTLS_RSA_CACULATION_ALT`。
  - AmebaD: 同 Ameba 机制，再加一份 `config_rom.h` 给 ROM 兼容用。
- 这一层是三家最干净的：upstream library 是真的 upstream，私有改动全在 `port/` 外挂。

### 风险点
- AmebaGreen2 把 mbedTLS ecp 部分代码烧进 ROM（`mbedtls-3.6.2/library_rom/ecp_rom.c`，搭配 `mbedtls_ram_map/rom/rom_ssl_ram_map.c` 函数指针表）。共用 3.6.5 时 ecp_rom 内部用的是 3.6.2 的接口，需要保留 v3.6.2 的 ECP 子集和 ROM 桥接代码。
- AmebaD 四个版本（2.4.0 / 3.2.1 / 3.4.0 / 3.6.0）共存说明历史上某些产品代码硬编码到老版本接口（很可能 linkkit、aliyun_iot），切到 3.6.5 要回归测 IoT-SDK。
- Bouffalo 同时维护 2.28.2 LTS + 3.6.5，部分代码（`blcrypto_suite` 基于 2.16.11 的 fork）锁在老版本，3.x 切换不能强推。

### 结论：✅ 容易抽，建议优先做
**实施步骤**：
1. 把 mbedTLS 3.6.5 做成项目级 git submodule（`third_party/mbedtls-3.6.5`），整 upstream tag，零本地 patch。
2. 三个 SDK 各保留自己的 `port/hw_acc/` 或 `hw_alt/` 目录（Bouffalo 5 个 alt、AmebaD 9 个 alt、Ameba 9 个 alt），通过 `MBEDTLS_*_ALT` 宏挂上去——这部分不动。
3. AmebaGreen2 的 ecp_rom.c 单独留为 chip-specific override（基于 3.6.2 ABI），其他算法用共用 3.6.5。
4. AmebaD 老版本 2.4.0/3.2.1/3.4.0 在 linkkit 弃用后删掉，统一到 3.6.5。
5. 长期把 Bouffalo 的 2.28.2 LTS 也淘汰，只留 3.6.5 一份。

---

## 整体建议

### 仓库结构
建议在 monorepo 顶层新增 `third_party/`，三个共用组件以 git submodule 形式放置：

```
third_party/
├── freertos-kernel/   # FreeRTOS V10.6.2 mirror + patches/
├── lwip/              # （长期目标，短期暂不抽）
└── mbedtls/           # mbedTLS v3.6.5 upstream + chip vendor 的 ALT 代码不在这里
```

各 SDK 子模块通过 CMake `add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/...)` 或 symlink + include path 引用，不修改 SDK 本体的目录结构（保持子模块 pristine，避免 fork submodule）。Ameba SDK 必须保留 `os_wrapper/`、`hw_alt/`、`port/realtek/` 等私有 port——这些是芯片绑定层。

### 跟现有 `platform/<sdk>` 子模块结构怎么共存
- 三个 SDK 仍然是 git submodule，**不 fork**。
- 共用基线 + chip vendor 私有 port 两层结构：上层（kernel/library）从 `third_party/` 取，下层（portable/port/hw_alt）从 `platform/<sdk>/` 取，编译期通过 include path / 链接器拼合。
- 构建系统在 `project/<board>/CMakeLists.txt` 里显式列出共用 + 私有源文件清单（参照本仓 `src/sources.cmake` 的做法）。

### 版本升级兼容风险
- **FreeRTOS 10.2.0 → 10.6.2**：AmebaD 跨大版本，`xTaskCreateRestricted`、static allocation API 行为有变化；`taskENTER_CRITICAL_FROM_ISR` 返回值类型在 10.4 改过；建议先跑 FreeRTOS 自测 + AmebaD 全部 BT/WiFi 回归。
- **lwIP 不动**：每个 SDK 维持当前版本，仅在 CVE 出现时三家分别 patch。
- **mbedTLS 3.6.5**：三家都已经走到 3.6.x，bouffalo_v3 已 3.6.5、ameba 3.6.5、amebaD 3.6.0，差距小，回归风险可控。AmebaD 升 0.5 个版本即可。

### 分阶段执行计划

**短期（1-2 周）**：
- 抽 mbedTLS 3.6.5 → `third_party/mbedtls/`，让 Ameba/AmebaD/Bouffalo（v3 部分）三家共用，保留各自 HW alt 实现。
- 删除 AmebaD 重复版本（2.4.0/3.2.1/3.4.0）确认无依赖后挪走。
- 建立 CVE watch：盯 mbedTLS upstream tag，统一 bump。

**中期（1-2 月）**：
- 抽 FreeRTOS 10.6.2 → `third_party/freertos-kernel/`，整理 `patches/realtek-add-xTaskGetPendedTicks.patch` 等 5-6 个补丁，提交给 FreeRTOS upstream 一份 RFC。
- AmebaGreen2 ROM 兼容层独立维护，构建条件分支即可。

**长期（半年+）**：
- 整理三家 lwIP 私有 patch（用脚本生成 vs upstream 2.1.2 的 diff），评估能上 upstream 的部分。
- AmebaD lwIP 升 2.0.3 → 2.1.2，砍掉双版本。
- 三家 lwIP 统一到 2.1.2 共用基线 + `patches/<vendor>/*.patch`。
