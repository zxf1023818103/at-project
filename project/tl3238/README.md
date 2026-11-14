# TL3238：FreeRTOS + NimBLE 1.10 + LE CoC + CS/RAS 测距 + mcumgr OTA

工程采用仓库现有的独立 CMake 工程 + `component` 目标方式，直接编译：

- `component/freertos-kernel_config/freertos-kernel`：FreeRTOS V11.1.0，包含队列、软件定时器和 `heap_4`。
- `component/freertos-cli_config`：现有 FreeRTOS-Plus-CLI V202212.00 包装目标。
- `component/nimble_config/upstream-1.10.0`：NimBLE 1.10.0 Host、FreeRTOS NPL 和 GAP/GATT 服务，通过 `component/telink_sdk_config/nimble_port` 接入；绑定使用 LittleFS 后端。
- `component/mbedtls_config/mbedtls`：Mbed TLS 3.6.4，编译 AES/CMAC/ECDH P-256 子集供 NimBLE 配对使用；OTA 启用 SHA-256。
- `component/nimble_config/ras`：RAS 按需服务端/客户端、标准数据编解码与双端 PBR 距离解算。
- `component/ble_coc_config`：跨芯片共用的 Flash Studio iOS L2C1 协议及 NimBLE LE CoC 服务；TL3238 移植层仅接入 CLI 和日志。
- `component/mcumgr_config`、`component/tinycbor_config`：mcumgr 0.7.0、TinyCBOR v7，以及仓库现有的 SMP over BLE 传输。
- `component/telink_sdk`：独立的平台驱动、普通 BLE/CS 原厂库、配套头文件、启动代码和链接脚本。
- `component/telink_sdk_config`：内存 HCI、FreeRTOS/tickless、NimBLE、OTA 和存储适配层。

本工程提供自己的 `freertos_config`，通过内核的 `A_CUSTOM_PORT` 接口接入 TL3238。仓库通用配置目前用于 Ameba/Bouffalo；不需要改动它或 FreeRTOS 子模块即可构建这个验证工程。

## 硬件与串口

已验证的连接：TL3238 **PA1=TX，PA2=RX**，USB 串口为 **COM19**。

| TL3238 | USB 串口 |
| --- | --- |
| PA1 / UART0 TX | RXD |
| PA2 / UART0 RX | TXD |
| GND | GND |

串口配置为 **115200、8N1、无流控**。接收使用 DMA2 的 4 × 128 字节循环链表和 512 字节 FreeRTOS stream buffer，避免 BLE 中断延迟导致 8 字节硬件 FIFO 溢出。下载接口沿用 SWS；应用不启用 JTAG 引脚路由。

## 构建

在仓库根目录执行：

```sh
make -C project tl3238
# 或
./project/tl3238/build.sh
```

依赖 Linux/WSL 中的 CMake ≥ 3.20、Ninja、Python ≥ 3.9，以及 Telink Andes GCC 12.2。工具链尚未安装时需 curl、tar/xz 下载；SDK 和 NimBLE 的必要文件已在 `component/`，构建不再下载或引用原 SDK checkout、CS ZIP、Eclipse 工程或 SDK CMake。其他开源组件仍使用仓库已有子模块。

- `component/telink_sdk/platform`：来自平台 V4.1.0，提交 `e88822226e4702910204e8599f44053065b68432`。
- `component/telink_sdk/ble/cs`：来自用户提供的定制 CS SDK。原 ZIP SHA-256：`840cd1aeb81f0ee7ce3f1dc1ec70ef8b08d42a5da0403c1d7010b719a94d415e`，Controller 库 SHA-256：`8fc4dcf1fdf35650f4bddc8b0667a6763dc8b8529474adb402e42524ab7f9c5a`。
- `component/telink_sdk/ble/standard`：关闭 CS 时使用，来自公开 BLE V4.0.4.8，提交 `b1edcb0b18b225f94da5eac45f7f4f8bafa6defd`。
- 工具链：Telink `V53xf_Linux_X64.tar.xz`，`riscv32-elf-gcc` 12.2.0，目标 `-mcpu=d25f -mext-dsp`。
- FreeRTOS：使用当前仓库子模块；本次验证提交为 `dcd24223fcc6163e96fd9479fb5b802f48092640`。
- NimBLE：来自 `nimble_1_10_0_tag`，提交 `a7a156f28954819e158b62dd613008f22f9cf73b`，本地副本已包含 `freertos-platform-compat.patch` 和 `cs-core60-hci.patch`，构建期间不再打补丁。共享的旧 NimBLE 子模块继续供其他工程使用。
- mcumgr：本仓库 `581f781`。

可通过环境变量指定工具链与输出目录：

```sh
TL3238_TOOLCHAIN=/path/to/nds32le-elf-mculib-v5f \
TL3238_BUILD_DIR="$PWD/build/tl3238" \
./project/tl3238/build.sh
```

输出位于 `build/tl3238/firmware/`：`tl3238_freertos.bin`、`.elf`、`.map` 和 `compile_commands.json`。启动代码和链接脚本在 `component/telink_sdk/startup/` 维护；构建目录只生成 RF 头文件覆盖层、去重后的库及带分区边界断言的链接脚本。组件清单和依赖审计见 [独立 SDK 组件说明](../../component/telink_sdk/README.md)。

默认 `TL3238_ENABLE_BLE=ON`、`TL3238_ENABLE_CS=ON`、`TL3238_ENABLE_OTA=ON`、`TL3238_ENABLE_COC=ON`、`TL3238_OTA_BUILD_NUMBER=1`。`TL3238_ENABLE_CS=OFF` 不加载 CS 库；`TL3238_ENABLE_OTA=OFF` 移除镜像管理；`TL3238_ENABLE_COC=OFF` 移除 CoC 服务并恢复原广播名。也可独立构建纯 FreeRTOS/CLI/tickless（同时关闭 CS/OTA/CoC）：

```sh
TL3238_ENABLE_BLE=OFF TL3238_BUILD_DIR="$PWD/build/tl3238-no-ble" \
./project/tl3238/build.sh
```

BLE Controller 使用官方预编译库。此板在 BLE SDK 原 V4.0.0 `sys_init` 中反复复位，因此复用已验证的平台 V4.1.0 系统、时钟和 PM 驱动；构建时从 Controller 库副本移除重复对象。RF 对象和 RF 头文件保持 BLE SDK 版本，确保私有校准结构 ABI 一致。源码 SDK 和原始库均不修改。

## SDK 组件拆分验证（2026-11-14）

已将 TL323X 平台、普通 BLE、CS 和启动配置拆入 `component/telink_sdk/`，NimBLE 1.10 移入 `component/nimble_config/upstream-1.10.0/`；应用、Hello、小型 OTA 原型和主机测试均使用组件内依赖。原 SDK 下载、解压和工程生成步骤已从构建入口删除。

- 全功能 `-O2` / `-Os`、关闭 CS、关闭 OTA、关闭 CoC、关闭 BLE，以及 Hello 共 7 种配置编译和镜像头检查通过；应用与 Hello 的 shell 构建入口也通过。
- 8 个构建目录的源码、include 搜索路径、Ninja 构建输入和实际头文件依赖审计通过，无原 SDK/cache 输入。
- 七种原厂 OTA 体积原型均通过，生成的 BIN 与拆分前逐字节一致。
- CS Host、RAS、LittleFS 绑定、mcumgr 内存、OTA、CoC 共 6 组已有主机测试通过；CoC 同时覆盖旧 NimBLE 子模块和组件内 1.10 版本。
- 同一构建目录中 CS `ON → OFF → ON` 编译通过，每次 RF/扩展驱动覆盖层均匹配所选版本，返回 ON 后 BIN SHA 恢复一致。

组件化全功能 `-Os` 镜像为 **370,370 B（361.69 KiB）**，SHA-256 `3bf1a21aab7c601dd26e0caebab976a7965fbd1b75b6707e7039d4268e594427`，比上一轮实测 BIN 增加 96 B；静态 data/BSS 大小相同。本轮重新组织了编译/链接输入，不能把不同 SHA 的新镜像视为已经完成上一轮板测。**本轮未烧录**。

记录与产物：[summary.json](../../build/tl3238-component-migration/summary.json)、[依赖审计](../../build/tl3238-component-migration/dependency-audit.json)、[新 `-Os` 镜像](../../build/tl3238-component-migration/full-os/tl3238_freertos.bin)。

## 组件化前完整 `-Os` 固件实板回归（2026-11-14）

保留 FreeRTOS/tickless、NimBLE、CS/RAS、LE CoC、LittleFS 绑定和 mcumgr OTA，完整 build 1 镜像为 **370,274 B（361.60 KiB）**，SHA-256 `74da09beb0c9a65b18ab27799c22be073fc0bfe44f772cbffcb49d795b44b262`。比本轮烧录前的 `-O2` 镜像 454,034 B 减少 **83,760 B（81.80 KiB）**；448 KiB 槽内剩余 **88,478 B（86.40 KiB）**。本轮使用独立构建目录，默认工具链仍为 `-O2`。

第一轮 `-Os` 板测发现 CLI 栈最低余量只有 464 B，未达到现有 512 B 检查门槛。将 CLI 栈从 768 增至 832 words（增加 256 B）后重新构建、烧录，CS/RAS 回归中最低余量为 **880 B**；测试门槛未降低。初始化后空闲 heap 为 **10,128 B**，本轮本地密码/解算测试记录的最低空闲 heap 为 **8,176 B**。这些是已执行路径的高水位记录，不代表真实 CS 或 CoC 吞吐测试的峰值。

| 已执行检查 | 结果 | 记录（相对 `build/tl3238-os-validation/`） |
| --- | --- | --- |
| SWS 烧录、镜像逐字节回读、CLI/FreeRTOS/tickless | 80 项通过 | `verification.json`、`flash.log`、`readback.bin` |
| CS 能力、RAS、本地 AES/P-256、合成距离解算 | 43 项通过 | `cs-ras.json` |
| 实际 BLE 广播、GATT 服务、CoC PSM 129、SMP 活动镜像 SHA | 19 项通过 | `coc-gatt.json` |
| 190 s BLE 持续阶段、分片/异常报文恢复、断连重连、远程复位与重连 | 219 项通过，总耗时 232.172 s | `ble-soak.json` |
| BLE OTA A/build 1 → B/build 2 | 通过，128.000 s | `ota-a-to-b.json` |
| BLE OTA B/build 2 → A/build 1 | 通过，134.938 s | `ota-b-to-a.json` |

首次 OTA 在 16,512 B 主动断连，重新连接后从同一偏移续传；错误平台、超长镜像、短 SHA、越界偏移、错误完整 SHA，以及不支持的试启动和激活等待期间的擦除请求均被拒绝。两次切换均确认新槽运行、活动镜像 SHA 匹配，并在重启后重新连接 SMP 服务。build 2 同为 370,274 B，SHA-256 `36fa3a68ec408a44dbd5735c89db7c25c7bb36545d28684849ef3f0e17d5c2df`。

完成 A→B→A 后再次 SWS 读取完整 **1 MiB Flash**：A 槽逐字节匹配 build 1，B 槽除已清零的启动标记外匹配 build 2；**60 KiB LittleFS 与升级前完全一致**，`0xF1000..0x100000` SDK/校准/MAC 保留区也完全一致。最终复位后 active=`0x00000`、build=1、BLE/Controller errors=0，恢复 `our=1 peer=1 cccd=3 writes=0`。核对结果见 `flash-preservation.json`、`storage-final.json`，原始 Flash 快照保存在仅本机用户可访问的 `private/`，包含绑定密钥。

该轮汇总见 [summary.json](../../build/tl3238-os-validation/summary.json)，已测试镜像为 [tl3238_freertos.bin](../../build/tl3238-os-validation/firmware/tl3238_freertos.bin)。历史配置参数保存在 `build/tl3238-flash-cost/full-os-command.json`；当前源码重建使用独立组件，产物应按新 SHA 单独验证。

测试脚本 `ble_verify.py`、`cs_verify.py`、`coc_verify.py` 可用 `--firmware` 指定被测镜像，并通过 `--output` 保留独立报告。`storage_verify.py` 会先采集 3 s 启动日志，再查询 CLI，避免 SWS 复位后的异步初始化日志干扰提示符匹配；首轮该问题的日志保留在 `storage-startup-race/`。CLI 栈余量不足的原始镜像和失败记录保留在 `first-attempt/`。

当前板卡没有天线，电脑此次测得 RSSI 约 −90 dBm。本轮通过结果限于上述场景；**未执行真实 CS 双端测距、手机 CoC 吞吐量或手机绑定重连验证**。本轮测试的是完整应用，小型原厂 BLE + Telink OTA 原型仍未烧录。

## BLE 与 mcumgr

默认设备名为 **`TL3238-CoC`**。主广播包含 Flash Studio CoC 服务 UUID，开启 CS 时同时包含 RAS UUID `0x185B`；扫描响应包含 SMP 服务 UUID 和名称。关闭 CoC 后恢复 `TL3238-mcumgr` 及原 SMP 广播。当前最多一个 ACL 连接，可作为 Peripheral 广播，也可通过 `cs connect` 作为 Central 连接对端；首选 ATT MTU 247。BLE 配置启用 LE Secure Connections Just Works 和 LittleFS 持久化绑定，容量为 4 个对端、16 条 CCCD；`cs pair` 发起配对，未启用 MITM。mcumgr 服务保持原来的访问方式。

| 项目 | 值 |
| --- | --- |
| SMP Service | `8d53dc1d-1db7-4cd3-868b-8a527460aa84` |
| SMP Characteristic | `da2e7828-fbce-4e01-ae9e-261174997c48` |
| 特征属性 | Write / Write Without Response / Notify |
| OS group | 0 |
| echo | command 0，CBOR 请求 `{"d":"hello world"}`，返回 `{"r":"hello world"}`；文本最多 127 字节 |
| reset | command 5，先返回 SMP 响应，再由 FreeRTOS 软件定时器延迟重启 |
| params | command 6，返回缓冲区参数 |
| info / boot info | command 7 / 8，报告实际平台及启动方式 |
| image group | 1：state / permanent confirm = command 0，upload = 1，erase inactive = 5 |

默认注册 OS 和 image 管理组，boot info 返回 `Telink ROM A/B (permanent)`。关闭 OTA 时 image 请求返回未支持。LittleFS 用于板端绑定存储，没有注册 mcumgr 文件系统管理组，因此 `fs unavailable` 表示未开放远程文件操作。SMP 服务保持实验工程原有的不要求认证的访问方式。

## Flash Studio iOS LE CoC

已按 `~/flash-studio-ios/L2CAPBench.swift` 实现发现、PSM 读取、双向字节流及双方确认。手机打开 **L2CAP → 连接 / 扫描 → 双向测速**，通道为 **PSM 129（0x81）**。App 当前显示的 `BL618DG` 是固定文字，无需修改即可使用 TL3238；扫描会自动连接首个兼容设备。

使用一个 2048 B RX SDU，流式处理最大 4096 B 应用帧；TX SDU 上限 245 B，credits 24，限制发送队列并预留 mbuf。只在打开 CoC 后请求 2M PHY、251 B DLE 和 15 ms 连接间隔。串口 `coc` 查看实际参数、双方计数、错误和流控统计。

完整操作、协议、内存和验证边界见 [LE_COC.md](LE_COC.md)，跨芯片接入见 [共用组件](../../component/ble_coc_config/README.md)。用户已完成原 TL3238 固件的手机测试；组件化后的构建另存并通过软件回归，尚未重新烧录或做手机测速。

## OTA 使用与边界

使用 TL3238 ROM 的双区启动能力和本工程原生 `.bin`，复用 mcumgr 的上传、状态和擦除处理器。镜像带有构建时注入的平台/格式标记及 uint32 build 号，版本显示为 `0.1.0.<build>`；无需添加 MCUboot 头。首次启用 OTA 需经 SWS 烧录本工程固件。

板上读取到 Flash MID `0x146085`、容量 1 MiB，布局如下。单个镜像最多 **448 KiB**，链接时也检查上限。

| 区域 | 镜像范围（结束地址不含） | 校验元数据扇区 |
| --- | --- | --- |
| A | `0x00000..0x70000` | `0x70000..0x71000` |
| B | `0x80000..0xF0000` | `0xF0000..0xF1000` |
| 保留 | `0x71000..0x80000`、`0xF1000..0x100000` | 包括 SDK 参数、配对、校准、MAC 地址区 |

mcumgr 的逻辑 slot 0 始终表示正在运行的镜像，slot 1 表示另一物理区。上传过程中不设置有效启动标志；逐页回读并校验完整 SHA-256 后才保存候选镜像元数据。`image state` 写入 `{"hash":<32-byte SHA>,"confirm":true}` 再次校验镜像，先启用新区，再禁用旧区；之后通过 OS reset 启动新区。未确认的完整上传不会改变启动区。激活等待重启期间禁止擦除/覆盖候选镜像。

只支持**永久切换**：`confirm=false` 的试运行请求返回 `MGMT_ERR_ENOTSUP (8)`，ROM 没有自动回滚机制。SHA-256 用于完整性校验，当前未实现签名认证或防降级。`upgrade=true` 可要求 build 号递增，但不是安全防降级机制。错误应用需通过 SWS 恢复；投入产品前需补充认证与签名策略。

在同一次设备运行期间，断开 BLE 后可用相同长度和 SHA 从 `off=0` 重新请求，设备返回已接收的偏移量；掉电/复位后的未完成上传从头开始。完整候选镜像的元数据可跨重启保存。

先保留正在运行的 build 1，另行构建待升级的 build 2：

```sh
TL3238_OTA_BUILD_NUMBER=2 TL3238_BUILD_DIR="$PWD/build/tl3238-ota-v2" \
./project/tl3238/build.sh
```

在安装了 `requirements-ble.txt` 的 Windows Python 中执行实际升级（会写入并重启指定设备）：

```powershell
python .\project\tl3238\ota_update.py --port COM19 --address 60:3F:FB:70:E5:D3 `
  --firmware .\build\tl3238-ota-v2\firmware\tl3238_freertos.bin `
  --negative-tests --resume-test --output .\build\tl3238\ota-upgrade-v2.json
```

脚本检查错误头、长度、SHA 和非法偏移，上传时主动断连后续传，再执行永久切换、重启、运行区/build 检查及远端活动镜像 SHA 对比。`--negative-tests` 会向非活动区写入一个故意校验失败的测试镜像，不会激活它。`--resume-test` 开启一次主动断连。串口 `ota` 可查看活动区、候选区、大小、进度和 pending 状态。日志与 JSON 保存在 `--output` 指定的位置。

仅检查接口可用性：

```powershell
python .\project\tl3238\ota_verify.py --port COM19
```

`ota_verify.py` 不写 Flash。不支持镜像管理时退出码为 2，接口存在但未执行升级时为 3，通信/检查出错时为 1。它报告的本地文件 SHA 不代表板端镜像验证。适配前返回 rc=8 的旧固件、全 Flash 备份及探测记录保存在 `build/tl3238/before-ota/`。

硬件端使用临界区内的 `flash_dread` / `flash_page_program` / `flash_erase_sector`。CS SDK 建连后会将 `flash_read_page` / `flash_write_page` 指针重定向为依赖中断的同步队列接口，不能在关中断区使用这些指针。Controller 初始化后恢复 FreeRTOS 端口要求的非嵌套中断设置。擦写期间 RF 中断会短暂延迟；本实现未保证 OTA 与实时 CS 测距并行的时序。

`bash project/tl3238/tests/run_ota_tests.sh` 使用真实 mcumgr/SMP/CBOR 和存储代码，仅模拟 NOR Flash，配合 ASan/UBSan 检查格式、续传、SHA 拒绝、A/B/A 切换及 130 种中断写入序列。软件故障注入不替代实物断电测试。

### OTA 实物验证（2026-11-14）

以下为接入 CoC **之前**的 OTA 镜像与测试记录，快照已保存在 `build/tl3238/before-coc/`。当前 CoC 固件的 SHA、内存和验证记录见 [LE_COC.md](LE_COC.md)；本轮验证了 SMP 镜像状态和活动 SHA，未重新执行完整 A/B/A 升级。

电脑通过 BLE 对 `60:3F:FB:70:E5:D3` 完成 **build 1 / A → build 2 / B → build 1 / A**。两份镜像均为 **416,978 B**；两次升级分别耗时 132.9 s、139.8 s，包含连接、校验、激活、重启及重连验证。首次在 16,512 B 主动断连，重连返回同一偏移继续上传。错误镜像 SHA、错误平台头、超长镜像、短 SHA、越界偏移、试启动和激活等待期间的擦除请求均被拒绝。

| 构建 | SHA-256 |
| --- | --- |
| build 1（接入 CoC 前的 OTA 基线） | `1b12073a6f977a9ddf5cc92e7a3ac599b46fbb3d17e524647741a94a8649d454` |
| build 2（OTA 测试镜像） | `28f948c9cb56cc2dea5aef1d38fd1eec53cb7c1c851a3873ed598683f995831b` |

每次重启后查询活动镜像，运行区/build 号及设备计算的完整 SHA 均符合目标。最终通过 SWS 回读 **1 MiB**：A 区与 build 1 逐字节相同；B 区与 build 2 仅启动标志字节不同（已清零）；两区元数据有效；`0x71000..0x80000` 和 `0xF1000..0x100000` 与升级前备份完全相同。回读记录为 `build/tl3238/ota-flash-verification.json`，原始 Flash 为 `ota-after-a-b-a.bin`。

升级后 CLI/tickless **83 项**通过，包含 61 s 长空闲及 30 s 持续运行；CS/RAS 本地 **43 项**通过，AES/P-256 与合成距离解算正常；BLE/mcumgr **90 项**通过，包含镜像查询、分片、异常报文恢复和重启。CS 双板无线测距未执行。默认、关闭 CS、关闭 OTA、关闭 BLE 四种配置均编译通过，硬件 OTA 验证使用默认 CS+RAS 配置。

相对于接入 OTA 前的内存优化版，Flash 增加 **11,248 B**，静态 RAM 分配增加 **184 B**（包含 16 B SWS Flash 阶段诊断记录）。heap 容量保持 38,912 B；空闲从 17,232 B 变为 **17,200 B**，差值为 `ota` CLI 注册节点。IRAM 余量 720 B，DRAM 扣除 2 KiB 启动栈后余量 144 B；这些余量与 heap 空闲分开统计。

汇总报告为 `build/tl3238/ota-summary.json`；无线升级记录为 `ota-upgrade-v2.json`、`ota-return-v1.json` 及同名 `.log`。使用 `sws_read.ps1 -Output <新文件路径>` 可再次只读备份本板 1 MiB Flash，脚本拒绝覆盖已有文件并在结束后复位。

### 分区与 LittleFS 核查（2026-11-14，密钥持久化适配前）

**本节保留适配前的只读审计记录，不代表当前固件状态；现行实现见下一节。** 当时没有 LittleFS 或持久化密钥。分区布局由 ROM A/B 启动约定、OTA 常量和 SDK 保留地址共同确定。以下地址均为物理 Flash 偏移，结束地址不含。

| 区域 | 地址范围 | 大小 | 本轮 SWS 回读情况 |
| --- | --- | --- | --- |
| A 镜像 | `0x00000..0x70000` | 448 KiB | 当前运行 build 1，428,722 B，与本地固件逐字节相同 |
| A OTA 元数据 | `0x70000..0x71000` | 4 KiB | 有数据，与此前 OTA 验证备份相同 |
| A 尾部保留 | `0x71000..0x80000` | 60 KiB | 全 `0xFF`，尚未分配给文件系统 |
| B 镜像 | `0x80000..0xF0000` | 448 KiB | 保留此前 OTA build 2，416,978 B，启动标志已禁用 |
| B OTA 元数据 | `0xF0000..0xF1000` | 4 KiB | 有数据，与此前 OTA 验证备份相同 |
| B 保留间隙 | `0xF1000..0xF2000` | 4 KiB | 全 `0xFF` |
| SDK SDP/ATT 缓存保留 | `0xF2000..0xF4000` | 8 KiB | 全 `0xFF`，当前未启用厂商 SDP |
| SDK SMP 保留（含注释备用范围） | `0xF4000..0xF8000` | 16 KiB | 全 `0xFF`，当前未启用厂商 SMP |
| B 其余保留 | `0xF8000..0xFE000` | 24 KiB | 全 `0xFF` |
| SDK 校准保留 | `0xFE000..0xFF000` | 4 KiB | 全 `0xFF`，仍保留给校准用途 |
| SDK MAC 保留 | `0xFF000..0x100000` | 4 KiB | 全 `0xFF`，仍保留给 SDK 地址用途 |

SDK 地址核对的是实际参与编译的 `drivers/TL323X/ext_driver/ext_ble_flash.h`。其 `FLASH_SMP_PAIRING_MAX_SIZE` 预处理结果为 **8 KiB**，但地址注释列出 `F4000..F7000` 四个扇区，因此上表完整保留 16 KiB；不能把注释中的 16 KiB 当成当前启用容量。`ACL_PERIPHR_SMP_ENABLE`、`ACL_CENTRAL_SMP_ENABLE`、`ACL_CENTRAL_SIMPLE_SDP_ENABLE` 均为 0，配对由 NimBLE Host 处理。

本轮只执行 SWS 读取，结束后复位，没有擦除、烧录或格式化。1 MiB 快照为 `build/tl3238/storage-audit-20261114/flash-full.bin`，SHA-256 为 `4e132521c63542bec70731919a0fa4d9fd878d5e0644fb5c978bc26a48ed9812`；逐区统计见同目录 `audit.json`。两区元数据和所有保留区与 `ota-after-a-b-a.bin` 完全一致。复位后串口确认 OTA active=`0x00000`、ready=1、Controller errors=0，记录见 `serial-after-read.log`。

审计时 LittleFS 与密钥存储的具体状态：

- [共用 LittleFS 配置](../../component/littlefs_config/lfs_config.h)使用 `0x300000..0x3F0000`、960 KiB、240 个 4 KiB 块，适用于已有的 4 MiB 平台。这块板只有 1 MiB，不能直接使用该地址或现成的共用 `lfs.bin`。
- TL3238 的 CMake、167 项实际编译输入和链接 map 中均没有 LittleFS；也没有 TL3238 的 `read/prog/erase/sync` 适配、挂载入口或文件 VFS。mcumgr 串口状态为 `fs unavailable`。
- [现有 `ble_store_lfs.c`](../../component/nimble_config/ble_store_lfs.c)供 AmebaD 使用，通过 newlib `fopen/fwrite` 保存 `/ble_store.bin`，未链接到 TL3238。其保存函数没有向 NimBLE 返回打开、写入、关闭失败，加载也可能接受部分记录；迁移前需要补齐完整性与错误处理，并验证中断写入后的恢复。
- [共用挂载代码](../../component/littlefs_config/lfs_config.c)在任何挂载失败后都会尝试格式化，未区分空白介质、损坏和 I/O 故障。用来保存密钥时需要明确空白初始化与故障恢复策略，避免挂载错误导致既有绑定被清空。
- 当前使用 NimBLE 1.10 的空 `store/config` 后端，`BLE_STORE_CONFIG_PERSIST=0`、`BLE_STORE_MAX_BONDS=0`、`BLE_STORE_MAX_CCCDS=0`，且 `sm_bonding=0`。它已解决配对请求不响应的问题，但不会保存 LTK/IRK/CCCD，所以重连仍可能再次要求配对。

审计建议将 **`0x71000..0x80000` 的 60 KiB（15 个 4 KiB 块）分配为 TL3238 独立 LittleFS 分区**。该范围当时为空白，在 OTA 擦写边界之外，并避开顶部 SDK 保留区。下面的实现已采用该布局。

适配前固件在 448 KiB 镜像上限内还剩 **30,030 B**。当时复位后 heap 总量 36,864 B、空闲 12,560 B；这些数值仅用于下面的增量比较。

### LittleFS 与持久化绑定（2026-11-14）

[flash_layout.h](../../component/telink_sdk_config/storage/flash_layout.h)统一定义镜像、元数据与 LittleFS 边界。LFS 固定使用物理 `0x71000..0x80000`，容量 60 KiB、15 个 4 KiB 块，读写缓存各 256 B、lookahead 8 B；A/B 两个运行区访问同一份数据。OTA 开启或关闭时，BLE 镜像都受 `0x70000` 上限约束。顶部 `0xF1000..0x100000` 继续保留。

[TL3238 存储适配](../../component/telink_sdk_config/storage/storage_port.c)通过实际 NOR 驱动读写，有分区边界检查、页写回读和擦除校验。只有挂载返回损坏、且整个 LFS 分区均为 `0xFF` 时才初始化；非空损坏或 I/O 错误不会触发格式化。挂载/绑定存储失败时不启动广播，保留 CLI 诊断。

[直接 LittleFS 绑定后端](../../component/nimble_config/store/ble_store_littlefs.c)保存本机 IRK、对端身份地址、LTK/IRK、密钥属性和 CCCD。760 B 文件使用显式字节布局、版本与 CRC，通过临时文件同步后原子改名提交。容量耗尽不自动删除其他绑定；写入失败传递错误并停止继续使用该存储实例。该后端不依赖 VFS，使用 256 B 调用方缓存；其他芯片采用更大 LittleFS 缓存时需相应调整，现有 AmebaD 后端未替换。

TL323X 在 resolving list 已加载成功后仍可能上报未解析的 RPA。[privacy.c](../../component/telink_sdk_config/nimble_port/privacy.c)在 NimBLE Host 创建连接前，用已存 IRK 校验 `ah`，唯一匹配后补充身份地址并保留空口 RPA。错误或有歧义的匹配不会选取绑定；加密过程仍校验 LTK。此处理位于 Host 任务，避免在 Controller 回调内获取 Host 锁。上游源码未修改，通过单文件编译符号重命名接入，兼容 LTO。

验证命令：

```sh
bash project/tl3238/tests/run_ble_store_tests.sh
bash project/tl3238/tests/run_ota_tests.sh
```

主机测试使用真实 LittleFS、NimBLE 存储读取 API、Mbed TLS 和 OTA 状态机，物理 Flash 由带边界检查的 NOR 模型替代。ASan/UBSan 覆盖密钥与 CCCD 重启恢复、容量、损坏文件、I/O 失败、36 个写入/删除掉电场景、模拟 OTA A/B/A，以及 Bluetooth Core D.7 地址解析向量、错误/缺失/歧义 IRK、4 个绑定中的正确匹配和连接事件字段保留。

Windows 实板验证脚本只读查询；`--write-probe` 写独立的非密钥测试文件，`--reset` 通过 SWS 复位并检查记录数量和测试文件保留：

```powershell
python .\project\tl3238\storage_verify.py --port COM19 --write-probe --reset
```

实板已验证 LFS 初始化、测试文件、1 个绑定与 3 条 CCCD 经 SWS 固件更新和复位保留，启动 `writes=0`。iPhone 13 Pro 在 RPA 修复后连续连接成功，用户确认不再弹出配对；板端记录 `host_resolved=1`、`encrypted=1`、`bonded=1`、`key_size=16`。首轮 HCI 共记录 7 次加密成功、0 次加密失败、0 个 SMP 报文，说明重连复用了已有绑定。同轮另有 1 次链路过程超时（`0x22`），当前无天线环境的射频稳定性不能据此判定通过。

实际无线 OTA **A build 1 → B build 2 → A build 1** 已通过，每次均核对目标区、build 和完整镜像 SHA。分别在升级前、B 区启动后、返回 A 区后用 SWS 读取物理 LittleFS 分区，**61,440 B 逐字节相同**，SHA-256 为 `72b1bf4b9282e75c63fb1a1e433ea37bd6c83e991e0a00353da2dc321ada0b1e`。返回 A 后再次复位，绑定计数、CCCD、独立测试文件均保留；用户再次确认 iPhone 13 Pro 连接成功且没有配对弹窗。B→A 首次检查紧接 SWS 复位，发生在 CLI 注册 `ota` 命令之前，因此在任何上传前停止；初始化完成后重试通过，相关失败记录也保留。

OTA 后 CS/RAS 本地 **43 项**检查与 CoC 合成协议自检通过，AES/P-256、合成距离计算和堆释放正常；这些本地检查不代表真实 CS 无线测距通过。汇总见 `build/tl3238/lfs-summary.json`；升级、三份私有分区快照及比对在 `build/tl3238/lfs-ota-preservation/`，最终存储/复位和 CS 回归分别为 `lfs-final-runtime.json`、`lfs-cs-regression.json`，HCI 汇总在 `ios-hci-20261114/iphone13pro-lfs-rpa.summary.json`。

持久化验收固件为 453,554 B，距 448 KiB 上限剩 5,198 B；相对适配前增加 24,832 B，静态 BSS 增加 8 B。存储状态从 heap 分配，heap 容量仍为 36,864 B，初始化后空闲 10,384 B。日志只输出数量与状态，不输出真实 IRK/LTK；HCI 和 Flash 原始备份包含密钥，应限制本地文件访问权限。

### 双方案分区和 Flash 写保护复核（2026-11-14）

本节的第一套方案是**Bootloader 内运行 mcumgr，直接更新单个 App**，不是 MCUboot 双槽交换。第二套是当前已经实测的 Telink ROM OTA A/B。第一套仍是设计方案，本次没有改为 Bootloader 启动，也没有启用独立块锁或永久锁；现有 SWS 烧录流程会先解除 Flash 保护。

后续按用户提出的“原厂单连接 BLE + Telink OTA”方向，新增了 [小型 Bootloader 体积验证工程](../tl3238_bootloader/README.md)。单连接、保留 SMP、采用本板平台驱动及 `-Os` 的镜像为 **102,484 B（100.08 KiB）**，静态 RAM **53,968 B**。这一替代传输方案可将 Bootloader 容量目标降到 **128 KiB**，并评估 **768 KiB 单 App**；下面的 448 KiB 共用地址与 320 KiB mcumgr 候选不代表原厂 OTA Bootloader 的最小需求。新工程只完成编译验证，原厂 A/B OTA 的旧镜像清理、启动标记切换和镜像大小限制仍需改造，尚未更改本板分区或迁移绑定。

实板新增只读命令 `storage flash`，读取 JEDEC ID、SR1/SR2、配置寄存器和前 128 B SFDP。当前结果：

- JEDEC 字节 `85 60 14`，SDK 整数 MID `0x146085`；SFDP 密度 `0x007FFFFF`，确认 **8 Mbit = 1 MiB**。
- TL323X SDK `flash_common.c` 将该内置器件列为 P25Q80SU。实际 SFDP 支持 QPI，厂商能力字 `0xFFFFE8D9` 声明独立块锁、锁位为易失、锁命令 `0x36`、上电默认为锁定；与 P25Q80SU 能力相符。P25Q80U 也使用相同 MID，但其公开 SFDP 表未声明独立块锁，因此不能只凭 MID 判断。完整封装/商业后缀仍应以 BOM 或实物标识为准。
- `SR1=0x00 SR2=0x02 CR=0x00`，即 `WPS=0 BP=0 CMP=0 SRP=0 QE=1`。**此次实测时没有启用 Flash 阵列写保护**。测量发生在现有 SWS 脚本执行 `lf 0 0`、固件正常启动之后，不代表未知出厂状态。
- SFDP 列出 4 KiB、32 KiB、64 KiB 以及 256 B 擦除能力；现有 OTA/LFS 仍只使用 **4 KiB 擦除、256 B 页编程**。擦除粒度不能当作独立锁粒度。

根据[普冉 P25Q80SU 官方手册](https://www.puyasemi.com/download_path/%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C/Flash/P25Q80SU_Datasheet_V1.5.pdf)第 6、10.4、10.51–10.55 节，独立锁模式需要 `CR.WPS=1`：首尾各 64 KiB 内按 4 KiB 锁定，中间区域按 64 KiB 锁定。配置 WPS 与临时解锁是不同操作；WPS 属于非易失配置，块锁本身是易失状态。对比来源为 [P25Q80U 官方手册](https://www.puyasemi.com/download_path/%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C/Flash/P25Q80U_Datasheet_V1.8.pdf)的 SFDP 表。

#### 两套方案共用的物理边界

建议先保持下表，以复用已验证的 `0x00000/0x80000` 镜像位置并保留现有绑定。所有结束地址不含；两套方案互斥运行，共用地址不表示能直接切换启动方式。

| 物理范围 | 大小 | 常驻 mcumgr Bootloader + 单 App | ROM OTA A/B（当前实现） |
| --- | --- | --- | --- |
| `0x00000..0x70000` | 448 KiB | Bootloader 代码槽，空间上限，不是预计实际体积 | App A |
| `0x70000..0x71000` | 4 KiB | Bootloader 控制/恢复记录，格式另行设计 | A OTA 元数据 |
| `0x71000..0x80000` | 60 KiB | 共用 LittleFS、绑定 | 共用 LittleFS、绑定 |
| `0x80000..0xF0000` | 448 KiB | 单 App 代码槽 | App B |
| `0xF0000..0xF1000` | 4 KiB | App 长度、校验、有效/更新状态，格式另行设计 | B OTA 元数据 |
| `0xF1000..0xFE000` | 52 KiB | SDK 保留及间隙 | SDK 保留及间隙 |
| `0xFE000..0xFF000` | 4 KiB | 校准保留 | 校准保留 |
| `0xFF000..0x100000` | 4 KiB | MAC 保留 | MAC 保留 |

第一套不是 MCUboot，不需要为双镜像 swap 预留第二 App 槽或 scratch。Bootloader 自身包含升级传输栈，不能直接假定只占 32/64 KiB。已有关闭 CS 的完整工程为 **306,606 B**，可作为容量参考，但不是已经完成的 Bootloader；448 KiB 槽能留出实现空间。现有关闭 OTA 的 App 为 **441,926 B**，仍能放入 B 槽。需要新增更新有效性提交、断电后停留在 Bootloader 的恢复策略，以及 TL3238 的 RAM/XIP 映射和入口交接；不能把普通函数调用当成完整启动。

这是一套兼容现有地址的基线，未追求单 App 最大容量。若接受重新分区，可进一步评估 **320 KiB Bootloader + 576 KiB App**：Bootloader `0x00000..0x50000`、App `0x50000..0xE0000`、App 元数据 `0xE0000..0xE1000`、LFS `0xE1000..0xF0000`、Bootloader 控制记录 `0xF0000..0xF1000`，顶部保留区不动。该候选在容量和 64 KiB 锁边界上成立，但 **`0x50000` App 的启动/映射尚未验证，不能直接烧录使用**；还需对 `0x71000` 的现有 LFS 做备份、迁移和恢复设计。本次不迁移，现有 OTA 代码也不能直接用于这一候选布局。

常驻 Bootloader 必须始终由 ROM 先启动，再决定是否启动有效 App；不得沿用 A/B 的“清除旧 A 启动标记”操作，因为那会绕过/禁用 Bootloader。具体镜像头、ROM 选择行为与软件跳转需单独验证。直接覆盖单 App 时旧 App 不再保留，失败后恢复的是下载能力；当前 ROM A/B 则保留旧运行区直到切换，但现有实现只支持永久切换，并没有健康确认后的自动回滚。

当前新增诊断后的 A/B 固件为 **454,034 B**，余量 **4,718 B**；该大小不能作为后续增加签名、回滚或保护模块仍能放下的保证，需要继续使用链接上限检查。未将顶部厂商保留区改作可分配空间，也没有迁移/格式化 LittleFS。

#### 配套写保护设计与当前缺口

建议以实板已声明的 **WPS=1 独立块锁 + 软件分区白名单**配合使用。只有 BP/CMP 的上下半区保护无法精确覆盖当前布局：例如锁低 512 KiB 会同时锁住 LittleFS；放开中间 LittleFS 区域也无法只用 BP/CMP 保持两个代码槽全部受保护。

| 写入场景 | Bootloader + 单 App 策略 | OTA A/B 策略 |
| --- | --- | --- |
| 普通 App 运行 | Bootloader 与 App 代码锁定；仅允许 LittleFS 事务 | 两个镜像与元数据锁定；仅允许 LittleFS 事务 |
| 上传/擦除 | 仅 Bootloader 允许写 B 的 App 槽及 App 元数据；A 的 Bootloader 代码保持锁定 | 只允许写非运行槽及对应元数据，运行镜像保持锁定 |
| 切换/提交 | 完整校验后提交 B 有效状态；保持 A 的 Bootloader 可启动 | 校验目标成功后，允许专门的激活函数写目标 `+0x20`，再清除原运行区 `+0x20` 的一个字节 |
| LFS 写入 | 只允许 `0x71000..0x80000` | 同左 |
| 厂商保留、校准、MAC、OTP | 常规代码拒绝擦写 | 同左 |

保护层应在底层 NOR 写入口统一执行：检查具体分区/阶段、长度溢出、页边界和擦除对齐；按实际锁粒度解锁目标，执行写擦及校验，所有返回路径重新锁定并用 `0x3D` 读回锁状态。写入与锁切换需统一互斥，不允许其他调用者绕开此入口。每次启动都应重新建立预期锁状态，不能把 MCU 复位等同于 Flash 上电复位。运行时避免全片解锁。

`0x70000..0x80000` 是一个中间 64 KiB 保护块，A 元数据/Bootloader 控制记录与 LFS 同块。写 LFS 时，这个 64 KiB 块在硬件上必须一起放开；软件仍应只准许 LFS 范围。该窗口不包含代码区，但不能宣称 A 元数据在 LFS 写期间仍受独立硬件锁保护。B 元数据 `0xF0000..0xF1000` 在尾部 64 KiB 内，可以单独按 4 KiB 开锁，保持 `0xF1000` 以上的保留区锁定。

当前 `QE=1`，写配置时必须保留 QE/读时序，不能把复用为 Quad IO 的 WP# 当作已经生效的独立保护脚。不得修改 OTP 锁、SRP 永久锁或安全启动熔丝来替代可恢复的块锁。厂商 `flash_lock_mid146085()` / `flash_unlock_mid146085()` 会将 WPS 清为 0，不能与独立锁方案混用。后续启用 WPS 前还必须验证 SWS 工具在该模式下的维修解锁与烧录流程，烧录完成后由固件恢复锁状态并读回确认。

**当前已有的是镜像大小断言、OTA 状态机边界和 LFS 地址范围检查；上表硬件锁流程尚未接入。** OTA 底层原始写擦入口还没有统一的保护事务，现有 SWS 脚本使用 `lf 0 0`，固件启动后未恢复硬件保护。本次只增加了诊断，没有通过危险的代码区/校准区试写来验证锁。mcumgr 当前也不要求认证，SHA-256 仅校验完整性；分区写保护不等于镜像来源认证。

实测和官方手册缓存位于 `build/tl3238/flash-protection-audit/`：`device.json` 是串口原始结果，`sfdp.bin` 和 `analysis.json` 为解析依据，`flash.log` 为诊断固件烧录回读记录。诊断固件单独构建在 `build/tl3238-flash-audit/firmware/`，SHA-256 `e2628109dcf56d5c8b88b4d70e0d50b50636a10a11bd172d463e604933e1ae02`；原有绑定 1 个、CCCD 3 条、启动写入次数 0，测试文件校验通过。

复用的 SMP 传输支持写入分片重组和通知分片发送；断连清除未完成报文，超过接收容量的长度头立即拒绝。mcumgr/TinyCBOR 兼容层保留属性解码错误，并拒绝不能容纳字符串结束符的文本。修复通过构建目录中的 cborattr 副本应用，未改动第三方子模块。

Host 与 Controller 通过 RAM 中的 HCI 队列通信，UART0 专用于 CLI。`nimble` 任务运行 Host/NPL，`ble_ll` 任务运行 Controller；中断通知和 HCI 入队均能唤醒 Controller，空闲时阻塞等待。

## SWS 烧录与自动验证

在 Windows PowerShell 中执行（先释放 COM19）：

```powershell
.\project\tl3238\verify.ps1 -Flash -Port COM19 `
    -BdtDirectory 'D:\MCU_Tools\BDT\release_v5.9.5'
```

`-BdtDirectory` 指向包含 `config\Cmd_download_tool.exe` 的 BDT 目录。本机也已验证 `C:\temp\tl3238_hello\bdt`。该脚本复用 `project/tl3238_hello/flash.ps1` 的 SWS 下载及回读实现，随后执行 CLI 专用验证；使用的是 BDT 5.9.5 原版命令行程序。它只擦写固件覆盖的扇区，按固件长度逐字节比对回读结果。

固件已经运行时，可只验证串口，或者增加持续运行检查：

```powershell
.\project\tl3238\verify.ps1 -Port COM19
.\project\tl3238\verify.ps1 -Port COM19 -SoakSeconds 190
# Check a 61-second task delay and cover the STIMER wrap:
.\project\tl3238\verify.ps1 -Port COM19 -LongIdleMs 61000 -SoakSeconds 190
```

脚本检查启动信息（`-Flash` 时）、帮助、命令输出、5 个基础任务（开启 BLE 时另有 `nimble`、`ble_ll`）、tick 频率、堆分配/释放、错误命令、参数错误、超长行、退格、Ctrl-C、CRLF 和 40 轮连续字节回显。还会对比 tickless 开/关时的实际 tick ISR 次数，检查 2、3、17、101、1200 ms 的睡眠补偿，并在睡眠中发送 UART 命令验证提前唤醒及完整接收。`-LongIdleMs 61000` 检查长任务延时；关闭 BLE 时还覆盖单次 60 秒上限，开启 BLE 时广播和连接事件会提前唤醒 CPU。

持续检查要求队列和软件定时器计数每轮增加；190 秒覆盖 24 MHz、32 位 STIMER 约 179 秒的回绕周期。脚本退出时释放串口，tickless 保持开启。

日志保存到 `build/tl3238/`：

- `flash.log`、`readback.bin`：下载、回读比对。
- `cli-COM19.log`：发送命令和实际串口响应。
- `verification.json`：通过/失败、固件 SHA-256、tick 频率、堆空间和错误计数。

`-Firmware` 可以指定其他输出位置；日志写入该固件目录的上一级。

### 真实 BLE/SMP 验证

使用带 BLE 适配器的 Windows 电脑，在独立 Python 环境安装测试依赖后运行：

```powershell
python -m pip install -r .\project\tl3238\requirements-ble.txt
python .\project\tl3238\ble_verify.py --flash --port COM19 `
    --bdt-root 'C:\temp\tl3238_hello\bdt' --rounds 12 --soak-seconds 190
```

测试通过电脑 BLE 适配器扫描、连接和订阅 Notify，实际编码并发送 SMP/CBOR 请求。覆盖普通与分片 echo、127/128/140 字节边界、params/info/boot info、不支持的管理组、超长报文恢复、残留半包后的断连重连、远程重启响应及重启后的再次连接。190 秒阶段交替进行无线 echo 和串口 selftest，检查跨 STIMER 回绕时的运行。Windows 可能在 Bleak 客户端退出后保留物理连接，因此测试用 `ble disconnect` 主动断开外设连接。

不加 `--flash` 时测试当前已运行固件，报告中的 SHA-256 来自本地构建文件；只有加 `--flash` 时才同时验证板上回读一致。脚本结束释放 COM19。结果保存为 `build/tl3238/ble-verification.json`、`ble-COM19.log` 和 `ble-flash-output.log`。不要同时运行两个串口验证脚本。

## 交互命令

启动后出现 `tl3238> ` 提示符。支持 CR 或 LF，命令最长 127 个字符；超长行整行拒绝。

| 命令 | 验证内容 |
| --- | --- |
| `help` | FreeRTOS CLI 注册命令列表 |
| `version` | 芯片、组件版本、CPU/tick/UART 配置 |
| `hello` | 输出 `hello world` |
| `echo hello world` | 串口接收与回显，包括空格 |
| `uptime` | 调度 tick 和毫秒运行时间 |
| `tasks` | 任务状态、优先级、剩余栈空间，单位为字节 |
| `heap` | `heap_4` 的总量、空闲、历史最小和最大块；CS+CoC 为 36 KiB，CS 无 CoC 为 38 KiB，无 CS 为 32 KiB |
| `stats` | 队列、软件定时器、FPU 及 UART 错误计数 |
| `tickless` / `tickless stats` | tickless 状态、实际 tick IRQ 数、WFI 次数、补偿 tick 数、定时器/UART 唤醒次数 |
| `tickless on` / `tickless off` | 运行时开启 tickless，或恢复周期 tick 供对比 |
| `idle 1200` | 暂停后台演示活动 1200 ms，检查长空闲；允许 2..65000 ms |
| `selftest` | 堆写入/读取/释放、后台队列、定时器、浮点上下文和串口错误检查 |
| `ble` | NimBLE 同步、连接计数、Host reset 和 Controller HCI/IRQ/错误计数（BLE 配置） |
| `ble disconnect` | 主动终止当前 BLE 连接，继续广播（BLE 配置） |
| `coc` / `coc selftest` | LE CoC 通道、链路参数、双方统计；无通道时运行合成协议自检（CoC 配置） |
| `ota` | OTA 活动物理区、build、镜像长度、上传偏移、pending 和 Flash MID（OTA 配置） |
| `storage` | LittleFS 挂载、分区、使用块数和绑定计数，不输出密钥（BLE 配置） |
| `storage test` / `storage check` | 写入并校验独立测试文件 / 只读校验该文件 |
| `storage flash` | 只读读取 Flash MID、SR1/SR2、配置寄存器和 SFDP，不读密钥、不更改锁位 |
| `storage clear-bonds` | 显式删除所有对端绑定及 CCCD，保留本机 IRK；手机端也需清除对应旧绑定后重新配对 |

后台 `producer` 每 10 ms 发送一个递增序号，`consumer` 校验顺序；软件定时器每 250 ms 触发。两个工作任务保留不同的浮点值跨越任务切换，并检查其正确性。正常情况下 `sequence_errors`、`fpu_errors`、`rx_dropped`、`rx_errors` 和 `tx_errors` 都为 0。

`idle` 命令暂时挂起 producer、停止演示软件定时器，并让 CLI 调用 `vTaskDelay`。到期后自动恢复后台活动，输出请求时间、RTOS tick 差值与独立读取的 STIMER 毫秒差值。CLI 在此期间阻塞，UART 中断仍正常接收；输入的命令保存在 512 字节 RX 缓冲中，窗口结束后执行。

## Tickless idle

默认 `configUSE_TICKLESS_IDLE=1`、`configEXPECTED_IDLE_TIME_BEFORE_SLEEP=2`。内核预计空闲至少 2 tick 时，移植层将 RTOS 定时器设置到最近的任务截止时间，执行 WFI。BLE 开启时，STIMER 比较器归 Controller 所有，开启 CS 时 RTOS 使用 TIMER1，TIMER0 用于 CS 数据处理触发；关闭 CS 的 BLE 配置仍使用 TIMER0。定时器共享只读的 24 MHz STIMER 计数作为时间基准。CPU 等待中断期间保留 BLE、UART/DMA、SWS、RAM 和外设时钟。这是 WFI 空闲模式，未接入 Telink 深度休眠/retention；这里记录的是 tick 中断减少量，未测量板级电流。

进入睡眠前，暂停调度器并屏蔽全局中断，检查 `eTaskConfirmSleepModeStatus()`、待处理中断和已到期 tick。WFI 保持 MEIE 使能，因此 BLE、UART 或 RTOS 定时器待处理中断可以唤醒 CPU。补偿完成前不执行 ISR，也不清除外设/PLIC 待处理事件；唤醒后再由原向量入口处理。

时间补偿保留原 tick 相位：对已经完整过去的 tick 使用 `vTaskStepTick`，最后一个到期 tick 交给正常 ISR 解锁任务，避免漏唤醒或重复计数。定时器设置过程中若已越过截止时间，会软件置位对应 RTOS 中断使其补算。单次睡眠最多 60 秒，低于 32 位 STIMER 的有符号半周期；更长空闲自动分段，因此无任务超时时也能维护系统时间。

实现依据为本仓库 FreeRTOS 内核的 idle/`vTaskStepTick` 代码和 Telink SDK `core.h`。可参照 [FreeRTOS 低功耗说明](https://github.com/FreeRTOS/FreeRTOS-Kernel-Book/blob/main/ch11.md) 的睡眠确认与 tick 补偿接口，以及 [RISC-V 中断规范](https://docs.riscv.org/reference/aia/v1.0/MSLevel.html) 的 WFI 待处理中断唤醒规则。

## 移植层范围

CS 配置按照定制 SDK 示例运行 CPU=96 MHz、HCLK=PCLK=48 MHz、MSPI=64 MHz，tick 为 1 kHz。TIMER1/IRQ3 驱动 RTOS，TIMER0/IRQ4 触发 CS 处理，STIMER/IRQ1、RF/IRQ15 驱动 BLE；UART0 RX 使用 DMA2 环形链表和 IRQ5。未开启 CS 的 BLE 配置仍为 CPU=48 MHz、HCLK=24 MHz、PCLK=12 MHz、TIMER0 tick。关闭 BLE 时恢复原 STIMER RTOS 定时器。以上入口均经过完整上下文包装，保存整数、FPU、FCSR、Andes MXSTATUS 和 DSP UCODE。栈保持 16 字节对齐，任务临界区嵌套由 FreeRTOS TCB 管理。主动切换任务时显式设置 MPP=M，适配 TL3238 的用户模式支持。

端口汇编、扩展寄存器保存及 `portmacro.h` 基于该固定 SDK 的 `3rd-party/freertos-V5/portable/GCC/RISC-V` 适配，保留原 MIT 许可。内核和 CLI 始终从本仓库的 `component` 编译。

此端口使用单核机器模式、独立 2 KiB ISR 栈，并关闭中断嵌套。`portYIELD_FROM_ISR` 记录调度请求，在 ISR 尾部统一切换任务。Tickless 在 idle 任务自己的栈上等待，不替换其上下文。新增调用 FreeRTOS API 的外设中断时，应添加同样的汇编上下文入口，不能直接在 SDK 的裸机 ISR 包装中切换任务。原始 `hello world` 工程保留在 `project/tl3238_hello`。

## Channel Sounding 移植

TL3238 工程固定使用 NimBLE **1.10.0**，不是以 Host 版本号声称完整 BLE 6.0 支持。上游稳定版及本次核对的 master 中，CS 仍有空的 terminate、reflector 配置和结果回调；本地 `component/telink_sdk_config/nimble_port/ble_cs.c` 替换这个实验实现，复用上游 HCI 分发和事件掩码。参见 [1.10.0 发布说明](https://cwiki.apache.org/confluence/spaces/MYNEWT/pages/435028314/RN-NimBLE-1.10.0) 和 [该版 CS 源码](https://github.com/apache/mynewt-nimble/blob/a7a156f28954819e158b62dd613008f22f9cf73b/nimble/host/src/ble_cs.c)。

已接入的范围：

- Telink CS Initiator/Reflector 初始化、配置/PHY/transport/HCI/PCT/DRBG 缓冲区，以及单天线能力设置。初始化返回值均检查；DRBG 参数使用 step 数量，而不是每步字节数。
- Host sync 后、广播/连接前，使用 LE Set Host Feature 设置 CS Host Support（bit 47），回读 LE features，确认 Controller CS（bit 46）和 Host Support 均启用；失败时 `ready=0`，保留 `host_rc`。`host_cs=1` 表示回读确认成功。
- 标准 CS HCI 命令 0x2089–0x2094，包括能力、默认设置、FAE、配置、CS 安全、过程启停。上游实验头文件缺少的 TX-SNR 能力和 Max_Procedure_Len 事件字段已通过补丁补齐，保留完整 [Core 6.0 HCI](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-60/out/en/host-controller-interface/host-controller-interface-functional-specification.html) 布局，并使用编译期尺寸检查。定制 SDK 缺少的 SNR-control 命令参数在 Controller 边界转换；不支持的 SNR 请求明确拒绝。
- NimBLE 发起流程：加密连接 → 能力交换 → 按对端能力交换 FAE → Mode 2 配置 → CS Security → 单次过程。ACL Peripheral 的安全过程由 Central 启动；反射端不会误发 Initiator 命令。支持停止、删除配置、异步失败反馈和断连清理。
- Result/Continue 变长步数据校验、分段和回调；超过 255 字节的 LL 报告按完整 step 切成标准 HCI 包，队列空间不足时整组拒绝并计入 `drops`。应用保存最近一个 subevent，支持 `cs data` 导出。
- TIMER0 仅唤醒高优先级 `ble_ll` 任务，IQ/DSP 处理在该任务中执行，允许 RF/STIMER 中断抢占；RTOS tickless 使用 TIMER1。ISR 栈及 CS/HCI 缓冲区使用 IRAM，链接时同时检查代码和 NOLOAD 数据的上界。

当前配置为 **PBR / Main Mode 2、无 sub-mode、单天线、单连接、config ID 0**。按 TL323X 示例分配的 transport buffer 不支持 RTT Mode 1/3；Host 会删除不匹配的对端配置。结果提供原始测量 step 和状态/频偏/功率信息，并接入下述 RAS 与距离解算。没有接入 Telink 私有 Inline-PCT 或 CS Test 射频测试命令。CS 两种角色与 ACL Central/Peripheral 角色彼此独立，参见 [Bluetooth CS 技术说明](https://www.bluetooth.com/channel-sounding-tech-overview/)。

### CLI 与保留的验证脚本

```text
cs caps                    # 通过标准 HCI 实际读取本机能力
cs                         # 状态、结果、校准返回值和错误计数
cs connect 603ffb70e5d3    # Central 连接指定 public 地址（示例，换成对端）
cs connect-random <12hex> # random 地址
cs pair                    # LE Secure Connections Just Works
cs remote                  # 查询对端 CS 能力
cs reflector               # 当前连接启用 Reflector
cs start                   # 加密后发起一次 Mode 2 过程
cs stop                    # 禁止当前配置的 CS 过程
cs remove                  # 删除 config 0；切换 CS 角色前先删除配置
cs security                # ACL Central 显式启动 CS 安全过程
cs data                    # 导出最近完整 subevent 的 step 原始十六进制
ble disconnect             # 终止连接并恢复广播
```

对端可以是另一块运行此固件的 TL3238（保持广播，连接后自动配置 Reflector），或能够发起标准 CS 的其他设备。两板必须各自使用不同 BLE 地址并接好天线。NimBLE 的应用接口见 `component/telink_sdk_config/nimble_port/include/host/ble_cs.h`，控制 API 从 Host event queue 调用；回调指针仅在回调返回前有效。

```sh
# Linux：真实生产解析器/状态机的 ASan + UBSan 测试
bash project/tl3238/tests/run_cs_host_tests.sh
bash project/tl3238/tests/run_ras_tests.sh
# 先完成一次 build.sh，供测试读取 NimBLE 和 TinyCBOR 生成头文件
bash project/tl3238/tests/run_mcumgr_memory_tests.sh
```

```powershell
# Windows：本地查询，不进行 RF 测试
C:\temp\tl3238_ble_venv\Scripts\python.exe .\project\tl3238\cs_verify.py --port COM19

# 接好天线、准备好 CS 对端以后再执行（替换地址）
C:\temp\tl3238_ble_venv\Scripts\python.exe .\project\tl3238\cs_verify.py --port COM19 --peer 603ffb70e5d3 --timeout 90
```

脚本输出 `build/tl3238/cs-verification.json` 和 `.log`。默认模式验证本地 HCI/CLI、RAS 合成数据及密码运算；带 `--peer` 才执行连接、配对、RAS 发现订阅、CS 流程、双端结果交换及距离解算，报告 `distance_mm`。原有 `ble_verify.py` 的 mcumgr 无线验证脚本保留，接天线后可重新运行；此前 1.9 基线的无线结果不能替代本次新固件的 RF 验证。

## RAS 与距离解算

编码按 [RAS 1.0 的公开 HTML](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/RAS_v1.0/out/en/index-en.html) 及 [Erratum 26610](https://files.bluetooth.com/download/erratum_26610_ranging_data_body_structure_update/)（v1.0，2025-08-11）实现。服务 UUID 为 `0x185B`，以下特征和 CCCD 写入均要求加密连接：

| 特征 | UUID | 属性 |
| --- | --- | --- |
| RAS Features | `0x2C14` | Read，返回 `0x00000004` |
| On-demand Ranging Data | `0x2C16` | Notify、Indicate |
| RAS Control Point | `0x2C17` | Write Without Response、Indicate |
| Ranging Data Ready | `0x2C18` | Read、Indicate |
| Ranging Data Overwritten | `0x2C19` | Read、Indicate |

支持 Get、ACK 和可选 Abort。尚未启用 Real-time、过滤、丢段重取，Features 不声明这些能力。通知与指示均开启时，按需数据使用通知；客户端默认订阅指示。传输按 ATT MTU 分段，校验 First/Last 和 6-bit rolling counter；完成响应、ACK 超时、背压、覆盖通知和断连清理均已实现。只保存一份完整过程，最多 32 个 subevent、256 个 step；超出容量或缺片会明确失败，不输出旧距离。

Central 加密成功后自动发现对端 RAS，读取 Features，发现实际 CCCD 句柄并订阅。Reflector 默认提供服务端数据；需要反向查询时可手动执行 `ras discover`。本端过程完成且对端 Data Ready 的计数器匹配后，自动 Get → 重组 → 计算 → ACK。双方的 configuration、过程计数器、subevent 起始 ACL 事件及 step 顺序必须匹配；不支持的格式会拒绝。

距离算法对两端的 12-bit I/Q PCT 做复数乘积，排除低质量/无效/扩展槽数据，按频道合并。利用相邻频道相关性估计初始斜率，跨频道间隔展开相位，再做最小二乘拟合，使用 `d = -slope × c / (4π)`；相位与频率关系可对照 [Zephyr 官方距离估计示例](https://github.com/zephyrproject-rtos/zephyr/blob/main/samples/bluetooth/channel_sounding/src/distance_estimation.c)。本实现为独立的纯 C 模块，不依赖 Telink Host/RAS 私有对象。

当前软件接受范围为 **0–60 m**，至少需要 15 个有效频道、30 MHz 跨度、8 对相邻频道，拟合残差不超过 0.35 rad。这个范围是算法门限，**不是硬件实测量程或精度承诺**。算法未实现 MUSIC/多径分离、RTT 融合或多天线估计。`DIST valid=0` 表示结果不可用；`residual_mrad`、`channels`、`pairs` 可用于诊断。

```text
ras                       # 服务、客户端、传输与距离状态
ras discover              # 加密后手动发现并订阅对端 RAS
ras get                   # 对当前本地过程重新查询对端数据
ras offset 123            # distance_mm = raw_mm - 123；单位 mm
ras offset 0              # 清除校准偏置，偏置不跨重启保存
ras selftest              # 无连接时，用合成 2.5 m 数据测试解算及 AES/P-256
```

双板流程：Reflector 保持广播；Initiator 执行 `cs connect <对端地址>`、`cs pair`，等 `ras` 显示 `client=1` 后执行 `cs start`，再查看 `ras` 的 `DIST valid=1` 与 `mm`。修改偏置后旧结果失效，需要重新测量。基于已知间距校准偏置时，应保留原始距离并在多个位置复测。

`client_state=0/1/2` 分别表示空闲、接收数据、等待 ACK 响应。验证脚本要求 `client_state=0` 且 `DIST valid=1`，因此会等完整的 RAS 操作结束。新过程启用、CS 报错或接收格式失败时，旧距离失效。

`ras selftest` 明确输出 `synthetic=1`，不会发布 RAS 记录或把合成结果设为实际测量。AES 使用 NIST AES-128 已知答案，P-256 使用公开的 `2×G` 共享点已知答案并通过 Controller RNG 做盲化；它们验证本地运算，不替代无线配对验证。为容纳双端记录，Mbed TLS 的不可变 AES 表通过 `MBEDTLS_AES_ROM_TABLES` 放在 Flash；距离工作区显式初始化到 IRAM。

已核对用户提供的勘误 26610 PDF 第 6 页表 3.7（含红色删除线）：Ranging Header 的第 4 字节由原 Antenna Paths Mask 改为低 4 位 Previously Used、高 4 位 RFU，头部仍为 4 字节，后续 subevent/step 偏移不变。依据 [Core 6.0 Vol 1, Part E §2.4.1/2.4.2](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-60/out/en/architecture%2C-change-history%2C-and-conventions/general-terminology-and-interpretation.html)，本端发送该字节为 `0x00`，接收时忽略全部 8 位；原先发送 `0x01`、并要求对端为 `0x01` 的逻辑已修正。因此能接收修订后头部，也不会仅因旧实现携带 `0x01` 或其他非零保留位而拒绝记录。单天线路径及未过滤的 Mode 2 数据长度由本地 CS 配置约束，不再从该字节推断。双端无线互操作仍需接天线后验证。

### 内存优化版本地实测（2026-11-14，接入 OTA 前的快照）

固件 **405,730 字节**，SHA-256 `6094e778e9ab9dcf0b8bcb86c10762b873893a98996a6bb0e39762d5c16435d0`。SWS 烧录、按固件长度回读逐字节一致。优化前的固件、map、验证记录及串口内存快照保存在 `build/tl3238/before-memory-optimization/`。

| 指标（字节） | 优化前 | 优化后 |
| --- | ---: | ---: |
| FreeRTOS heap 容量 | 32,768 | 38,912 |
| 空闲 heap | 8,528 | **17,232** |
| AES/P-256 自检期间最低空闲 | 6,576 | **15,280** |
| 最大连续空闲块 | 8,432 | 17,136 |
| IRAM 链接余量 | 464 | 720 |
| DRAM 未分配空间（已扣除 2 KiB 启动栈） | 5,128 | 328 |

空闲 heap 增加 **8,704 B（约 102%）**。其中，减少任务栈预留 2,560 B；回收 mcumgr 重复请求缓存 1,152 B、只读 GATT 定义 192 B、CS 诊断缓存含对齐净值 256 B；实际减少 RAM 预留合计 4,160 B。DRAM 的部分未分配空间也并入 heap，因此 heap 容量增加 6 KiB。这些是同一片 RAM 的不同用途，不能把 heap 空闲与原有未分配空间重复相加。

- TL3238 为 mcumgr 启用 `MCUMGR_SMP_BORROW_REQUEST=1`：在 NimBLE Host 任务中同步读取 BLE 重组缓冲区，处理返回后清除借用指针。发送失败后 SMP 可能尝试复用请求作为错误响应，此路径改为使用独立响应缓存。1152 B 报文容量保留；其他平台默认仍复制请求。
- RAS/SMP 特征定义改为只读，存放在 Flash。CS 原始诊断缓存由 1536 B 改为 SDK 上限 `75 × 17 = 1275 B`，并通过编译期断言校验；Controller FIFO 和 RAS 的 32 subevent / 256 step 容量保留。
- 链接检查包含 `.sdk_version` 和末尾对齐，保证完整 DRAM 分配与 2 KiB 启动栈不重叠。独立 ISR 栈为 2 KiB。

| 任务 | 原栈大小（B） | 当前栈大小（B） | 回归后最小剩余（B） |
| --- | ---: | ---: | ---: |
| cli | 4096 | 3072 | 720 |
| producer / consumer | 各 1536 | 各 1024 | 各 592 |
| Tmr Svc | 1536 | 1024 | 640 |
| nimble | 6144 | 6144 | 3584 |
| ble_ll | 4096 | 4096 | 2928 |
| IDLE | 1024 | 1024 | 624 |

83 项 CLI/tickless 检查通过，含 61 秒长空闲、30 秒后台队列/FPU/定时器检查；周期/tickless 在约 2 秒内分别触发 2037/242 次 tick ISR，UART/队列/FPU 错误为 0。43 项 CS/RAS 检查通过，含三轮合成 2.5 m、AES、P-256 自检以及任务栈至少剩余 512 B 的检查，heap 前后稳定。

RAS ASan/UBSan 回归通过。新增 mcumgr 测试分别运行原复制模式与借用模式，使用真实 SMP/CBOR 核心和 BLE 重组，仅替代 ATT 边界；覆盖只读映射请求、发送失败、最大 1152 B 报文、ATT 分片、同一次写入的多请求、1000 次缓冲区复用及畸形/断连恢复。完整 CS/RAS、BLE 无 CS、无 BLE 三种配置均编译通过；SDK 原有 LTO 类型警告仍在。

对比汇总为 `build/tl3238/memory-optimization.json`；板端记录为 `verification.json`、`cs-verification.json` 及对应日志，mcumgr 本地测试记录为 `mcumgr-memory-tests.log`。当前无天线，**尚未验证真实无线连接和 CS/RAS 测距时的峰值内存与栈用量**；无线验证脚本保留。

与用户提供的原版 CS SDK 实际编译对比见 [SDK_MEMORY_COMPARISON.md](SDK_MEMORY_COMPARISON.md)。原厂默认 Initiator / Reflector 分别预留 126.39 / 96.92 KiB RAM；改为单天线＋RAS 后为 121.66 / 91.48 KiB。该阶段完整固件预留 156.98 KiB（含 38 KiB FreeRTOS heap），比单天线 RAS Initiator 多 35.31 KiB；角色、内存池、算法和附加服务不同，不能将差额归于 NimBLE 本身。统计包含 RAM 驻留代码和未初始化日志缓冲，原厂基线尚未烧录测峰值。当前 CoC 固件另见 [LE_COC.md](LE_COC.md)。

### RAS 勘误修正基线（2026-11-14，内存优化前）

固件 **405,682 字节**，SHA-256 `c25884e7bc0de185a6fb27f3125ba7cf4f7d2516b01110994c3494666e329d7f`。SWS 烧录后按固件长度回读，逐字节一致。修正前固件及验证记录保存在 `build/tl3238/before-ras-erratum/`。

- 77 项 CLI/tickless 检查通过，包含 61 秒长空闲；约 2 秒内周期/tickless 分别为 2048/237 次 tick ISR，UART/队列/FPU 错误为 0。
- 42 项 CS/RAS 本地检查通过；三次 `ras selftest` 均输出 `synthetic=1 expected_mm=2500 measured_mm=2500 channels=72 residual_mrad=0 AES=PASS ECDH=PASS`，测试前后 heap free=8528。P-256 运算期间 heap minimum=6576。
- 勘误回归测试先在修正前代码上复现头部不符合预期，修正后 ASan/UBSan 全部通过。独立字节向量校验 4 字节头部；穷举第 4 字节的全部 256 个接收值，均保持正确解算；实际服务端发送零、实际客户端回调接收 `0x00/0x01/0x80/0xFF` 均完成 Get→解算→ACK。ATT/NPL 边界由测试替代，未将这些结果当作无线互通验证。
- ASan/UBSan 测试覆盖 HCI→RAS 的 Mode 0 角色差异、多 subevent、aborted step、MTU 23/247 分段与计数器回绕、Get/ACK/Abort、加密/CCCD、背压/超时、实际客户端发现订阅回调、丢片与断连清理。CS 与 RAS 解析器分别通过 100000 组畸形输入；合成 PBR 验证覆盖 0.2/2.5/10/25/50 m。
- CS/RAS 和 BLE 无 CS 配置编译通过；无 CS 固件与前一基线哈希一致。新代码无编译警告，仍保留定制 SDK 内部 `rf_fast_settle_config` 返回类型的 LTO 警告。

本节板端记录已归档到 `build/tl3238/before-memory-optimization/`。当时板上无天线，**未执行该版固件的无线配对、RAS 双端互操作或真实距离/精度测试**。自检结果不能作为射频精度结果；`--peer` 验证入口已经保留。

本次上传的勘误 PDF 副本保存在 `build/tl3238/ras-reference/Erratum_26610_Ranging_Data_Body_Structure_Update.pdf`，SHA-256 `08608510a6c9008983dfb7bbd0b99fec073bb36235c30793ff45bb3d2bcc9cc0`。修正前失败记录为 `build/tl3238/ras-erratum-regression-before.log`。

### CS 移植基线（2026-11-13，RAS 接入前）

基线固件 **382,310 字节**，SHA-256 `793debfe08d5db46d0d0d64195394676b1543dec25b337fc3e02de3ce5e5872f`，SWS 回读逐字节一致，固件及报告保存在 `build/tl3238/before-ras/`。

- 最终固件 77 项 CLI/tickless 检查通过，含 61 秒 idle；周期/tickless 在约 2 秒内分别触发 2042/239 次 tick ISR，测试前后 heap free=8960，UART/队列/FPU 错误为 0。
- 26 项 CS 本地检查通过，包括无连接命令拒绝、参数校验和重复 HCI 查询。实际 HCI 查询：`ready=1 configs=1 roles=0x03 antennas=1 paths=1 tx_snr=0x00 host_cs=1`，初始化成功且 Host Support 已回读确认；`calibration=1` 表示 Flash 中没有有效 CS 校准表，未写入或伪造校准数据。
- 离线测试覆盖 CS Host Support 设置/回读及失败、Core 6.0 字段长度、CS 角色、加密前置条件、失败终止、启停命令、75-step 报告的 4 包分段与精确还原、背压、断连以及 100000 组畸形数据；ASan/UBSan 通过。
- CS / BLE 无 CS / 无 BLE 三种配置均编译通过。SDK 自带的 `ext_fast_settle.o` 与 `rf_common.o` 对 `rf_fast_settle_config` 返回类型有 LTO 警告；两者都来自同一定制库，保留警告，未修改闭源库。RF 行为仍需天线实测。

板子目前没有天线，未运行新固件的无线配对、CS 双端交换、测量稳定性或测距精度测试。`cs_verify.py --peer` 和 `ble_verify.py` 已保留这些后续验证入口。

## 原 NimBLE 1.9 固件基线（2026-11-13，CS 移植前）

此前基线固件 **205,546 字节**，SWS 按固件长度回读逐字节一致。SHA-256：`db46c16c274bebc5f85bb7f6650ec91f3486322e89a1ac83951b47e37a961db8`。BLE 开启、关闭两种配置均编译通过。

COM19 上 **77 项检查通过**，BLE 广播保持运行，测试耗时 73.4 秒：

| 检查 | 实测结果 |
| --- | --- |
| 周期 tick / tickless，约 2 秒 | 2041 / 242 次 tick ISR，减少约 88% |
| 61 秒延时 | `elapsed_ticks=61000`，`hardware_ms=60999` |
| UART DMA | 40 轮连续回显、超长行、退格、Ctrl-C、CRLF、空闲中提前唤醒全部通过 |
| 堆空间 | 测试前后均空闲 10,096 字节 |
| 后台任务 | 队列顺序、软件定时器、FPU 上下文正常 |
| 错误计数 | `sequence_errors=0 fpu_errors=0 rx_dropped=0 rx_errors=0 tx_errors=0` |

该基线固件和 CLI/BLE 验证记录保存在 `build/tl3238/before-channel-sounding/`。当前 `build/tl3238/verification.json`、`cli-COM19.log`、`flash.log`、`readback.bin` 对应 CS 新固件。

无线调试发现板子未接天线，电脑接收到的广播 RSSI 约 **-98～-99 dBm**。已验证标准 SMP 服务发现、echo/分片、params/info/boot info、非法长度拒绝和后续请求恢复；190 秒无线持续测试在运行中出现响应超时，未计为通过。该次记录保留为 `build/tl3238/ble-weak-signal-soak-verification.json` 和 `ble-weak-signal-soak-COM19.log`。按用户安排保留完整验证脚本，接好适配的 2.4 GHz 天线后再运行 `--soak-seconds 190`。

最终 DMA 固件另做了一次短测：服务发现、上述 OS 命令、127/128/140 字节边界以及带残留半包的断连重连通过；第二次连接中仍发生请求超时，完整脚本结果为 **FAIL**，远程重启及重启后重连步骤尚未执行。基线结果见 `build/tl3238/before-channel-sounding/ble-verification.json`、`ble-COM19.log`。当前不把无线稳定性或远程重启列为已验证项目。

## 原 tickless 基线（2026-11-13，未开启 BLE）

引入 BLE 前的 tickless 固件 33,437 字节，SWS 回读与固件逐字节一致。COM19 上 114 次检查全部通过，总耗时约 265 秒，包含 61 秒长空闲和 190 秒持续运行：

| 检查 | 实测结果 |
| --- | --- |
| 相同负载下约 2 秒的 tick ISR 数 | 关闭 tickless：2050；开启：229，减少约 89% |
| 61 秒空闲补偿 | `elapsed_ticks=61000`，STIMER 实测 `hardware_ms=60999` |
| 单次睡眠上限 | `max_idle_ticks=60000`，超过上限自动分段 |
| UART 提前唤醒 | 1.2 秒空闲中途发送命令，完整接收且原截止时间保持正确 |
| STIMER 回绕与后台任务 | 190 秒持续检查通过，队列、软件定时器、FPU 均无错误 |
| 堆及串口 | 堆空闲前后均为 21,056 字节，RX 丢字节/接收/发送错误均为 0 |

该基线 SHA-256：`a2cf73fddf2f96a8d6f10f94c515163f8068c9e4473f9919fdad47953696e79d`。固件及验证记录备份在 `build/tl3238/before-nimble/`。原周期 tick 固件和记录备份在 `build/tl3238/before-tickless/`。
