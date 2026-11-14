# TL3238 原厂 BLE + Telink OTA：小型 Bootloader 体积验证

2026-11-14 实际编译：单从机连接、保留 SMP 和真实 Telink OTA Server，使用当前板卡已适配的平台驱动，最终镜像 **102,484 B（100.08 KiB）**，静态 RAM **53,968 B（52.70 KiB）**。可以将后续常驻 Bootloader 的容量目标收敛到 **128 KiB**，目前剩余 **28,588 B（27.92 KiB）**，无需继续按先前完整 NimBLE/mcumgr 工程预留 320 KiB。

本工程目前是**编译及镜像检查用的 OTA 原型**。仍保留原厂 A/B OTA 生命周期，尚未实现常驻 Bootloader 的单 App 覆盖、有效性提交、断电恢复和启动交接。没有烧录、修改实板分区或启用新的 Flash 锁。现有 NimBLE、LittleFS 和手机绑定不受本次编译影响。

## 使用的原厂栈与裁剪范围

[TL323X 官方产品页](https://www.telink-semi.com/products/bluetooth-le/tl323x)提供的是 Multi Connection SDK。本次使用其中支持 TL323X 的 `liblt_TL323X.a`，配置 `ACL_CENTRAL_MAX_NUM=0`、`ACL_PERIPHR_MAX_NUM=1`；这是原厂栈的单连接配置，不是把 B91/TC32 的专用 Single Connection 二进制移植到 TL3238。

- BLE 源码、头文件和库：`component/telink_sdk/ble/standard`，来源提交 `b1edcb0b18b225f94da5eac45f7f4f8bafa6defd`。
- 平台驱动：`component/telink_sdk/platform`，来源提交 `e88822226e4702910204e8599f44053065b68432`。
- 编译器：Telink V53xf GCC 12.2.0，Andes D25F，LTO。
- 基线来自官方 Eclipse `acl_peripheral_demo`；源文件列表和编译/链接选项已固定在组件的 `acl_peripheral.json`，构建不再读取 SDK Eclipse 工程。
- `lean-*` 使用裸机主循环；GATT 只保留 GAP、GATT、Telink OTA，删除 HID/Battery/SPP 服务；关闭 UI、调试输出、PM/deep retention。OTA 数据处理 `otaWrite`、Flash 写擦和完整性校验仍来自原厂库。
- 所有配置均保留 `BLE_OTA_SERVER_ENABLE=1` 和 `APP_FLASH_PROTECTION_ENABLE=1`，没有通过关闭 OTA 或 Flash 保护来压低数字。原厂保护回调仍是参考实现，**尚不是按本板分区改造的独立块锁策略**。
- `lean-smp-board-os` 使用与当前应用相同的 V4.1.0 system/clock/PM/普通外设驱动和 LDO 初始化；RF 库对象及其私有 ABI 头文件保持 BLE SDK 版本。只修改构建目录中的副本。

## 实测编译结果

| 配置 | 连接数 | SMP | 优化 | 最终 BIN（B） | 静态 RAM（B） |
| --- | ---: | --- | --- | ---: | ---: |
| `stock-4`：原厂完整外设示例 | 4 | 开 | `-O2` | 129,012 | 66,904 |
| `stock-1`：只改连接数 | 1 | 开 | `-O2` | 127,284 | 65,368 |
| `lean-smp`：精简服务/UI/PM | 1 | 开 | `-O2` | 112,164 | 56,280 |
| `lean-open`：同上，关闭 SMP 初始化 | 1 | 关 | `-O2` | 109,412 | 56,280 |
| `lean-smp-os` | 1 | 开 | `-Os` | 103,348 | 54,476 |
| `lean-open-os` | 1 | 关 | `-Os` | 100,820 | 54,364 |
| **`lean-smp-board-os`：采用板卡平台驱动** | **1** | **开** | **`-Os`** | **102,484** | **53,968** |

只减少连接数节省 1,728 B Flash；主要收益来自删除附加服务/UI/调试/PM 和 `-Os`。在同一套原厂驱动下，关闭 SMP 仅再省 2,528 B，后续建议以保留 SMP 的版本为起点。关闭 SMP 初始化并不意味着库内所有 SMP 通用代码和数据都能被链接器移除。

SMP 是 BLE Security Manager Protocol，此处不是 mcumgr 的 Simple Management Protocol。原厂示例启用的是未认证配对加密；当前 OTA 属性仍使用普通读写权限。**启用 SMP 不代表已强制 OTA 加密访问或验证镜像签名**，这部分尚未适配。

进一步核对后，以上 `lean-smp-*` 沿用的是原厂默认 Legacy Pairing，未启用完整应用中的 LE Secure Connections/P-256。补充通过原厂公开 API `blc_smp_setSecurityParameters_periphr(Bondable_Mode, 0, LE_Secure_Connection, 0, 0, IO_CAPABILITY_NO_INPUT_NO_OUTPUT)`、在 `blc_smp_smpParamInit()` 前启用 LESC：板卡驱动版本为 **110,324 B（107.74 KiB）**，比默认 SMP 增加 **7,840 B**，静态 RAM **54,408 B**。ELF 包含 `blt_smp_sc_handler`、`eccp_pointmul` 和 PKE 硬件驱动。若后续 Bootloader 需要与当前 App 对齐 LESC，应以此版本为起点估算，128 KiB 内剩余 **20,748 B（20.26 KiB）**。该补充只构建在 `build/tl3238-flash-cost/vendor-lesc/`，未更改上述默认配置，也未做无线配对验证。

Flash 数字包含原厂 `check_fw` 后处理后的对齐填充及末尾 CRC32，不是只数 ELF `.text`。构建逐一检查头部长度、ROM boot marker、Flash-protection flag 和 CRC32，并确认 OTA 收包、保存数据、完成处理函数实际在 ELF 中。

RAM 统计包含 RAM 中的代码、retention 数据、NOLOAD 缓冲、data/BSS、SDK version 及对齐，按 ELF 实际分配段和边界符号计算，不能只看 `.bss`。板卡驱动版本的 IRAM 为 **53,616 B**，DRAM 为 **352 B**；160 KiB 总 RAM 中尚未分配 **109,872 B**，扣除 2 KiB 链接栈预留后为 **107,824 B**。这是链接容量，**不是实测运行时空闲堆或栈峰值**。纯 SDK 对照保持原链接器的 600 B 栈边界检查；板卡版本改为以 `_end` 检查 2 KiB，并检查 IRAM NOLOAD 缓冲和最终镜像的 128 KiB 上限。

七种配置均编译、链接、镜像格式和 CRC 检查通过。只有工具链提示 LTO 的 3 个 LTRANS 任务串行编译，无 C 编译警告。无线收发、实际 OTA 写入和运行时栈用量尚未在这些镜像上验证，不能把编译通过当成小型常驻 Bootloader 已完成。

## 与当前完整应用的 Flash 差额

同日补充体积对照，仅修改构建目录中的优化参数，保持源代码、功能开关及断言配置；未将 `-Os` 设为应用默认值。后续完整 `-Os` 应用已烧录并完成回归，CLI 栈增加 256 B 后镜像大小不变，详见 [完整应用实板记录](../tl3238/README.md)。以下原厂原型和关闭 CS 的对照镜像仍未烧录：

| 比较对象 | 最终 BIN（B） | KiB |
| --- | ---: | ---: |
| 当前完整应用，`-O2` | 454,034 | 443.39 |
| 完整功能不变，仅改 `-Os` | 370,274 | 361.60 |
| 关闭 CS/RAS、改用普通 Controller，其余功能保留，`-O2` | 307,086 | 299.89 |
| 同上，改为 `-Os` | 242,142 | 236.47 |
| 原厂精简 BLE + OTA + LESC，板卡驱动，`-Os` | 110,324 | 107.74 |
| 原厂精简 BLE + OTA + Legacy SMP，板卡驱动，`-Os` | 102,484 | 100.08 |

完整应用单独改变优化选项可减少 **83,760 B（81.80 KiB）**。在同为 `-Os` 时关闭 CS/RAS 并更换 Controller SDK 减少 **128,132 B（125.13 KiB）**，这个差额同时包含中央角色、CS Controller/HCI、RAS、解算及相关库的变化，不能算成仅 RAS 协议或 NimBLE Host 的大小。

剩余差额包括 FreeRTOS/NPL、HCI 传输与通用命令处理、NimBLE Host、软件密码算法、mcumgr/CBOR、LittleFS/绑定持久化、CoC 和 CLI/验证功能。原厂原型是直接调用 Controller 的裸机 GAP/GATT/OTA；当前应用保留标准 HCI 路径，其无 CS、`-Os` ELF 中仅 `blc_hci_cmd_handler` 一个函数就有 **13,664 B**，并引用扩展/周期广播、连接建立等处理。原厂原型没有此通用命令分发函数。

安全实现也不同：当前 NimBLE 采用 mbedTLS 的 AES、P-256/ECDH 和 OTA SHA-256；补充原厂 LESC 构建链接了芯片 PKE 驱动。100 KiB 的 Legacy SMP 原型与当前安全配置不等价，110,324 B 的补充结果只补齐了 LESC 这一项，仍不是完整应用的同功能对照。

`build/tl3238-flash-cost/comparison.json` 保存 ELF/BIN 路径、SHA-256，以及按函数起始 DWARF 地址归类的代码统计。后者去重同地址别名，但不含常量，并受 LTO 内联影响，不能把它们相加当作逐模块移除的准确收益。`summary.json` 保存本表及编译差额。堆、任务栈、零初始化缓冲主要消耗 RAM，60 KiB LFS 分区也是存储容量，均不能直接计为相同大小的固件 Flash 代码开销。完整 `-Os` 应用的 SWS 回读、CLI/tickless、CS/RAS 本地测试、190 s BLE 持续阶段和 A→B→A OTA 已通过；真实 CS 测距和手机 CoC 吞吐量未在该镜像上测试。记录位于 `build/tl3238-os-validation/`，体积对照保留栈调整前的原始镜像路径与 SHA。

## 对常驻 Bootloader 分区的影响

原厂 Telink OTA Server 不能只改一个地址就作为“128 KiB Bootloader + 大 App”使用。[官方 OTA 手册](https://doc.telink-semi.cn/doc/en/software/res/sdk/ble/tl_ble_sdk_multi_connection_en/tl_ble_sdk_multi_connection_en/)的限制是：

- `blc_ota_setFirmwareSizeAndBootAddress()` 的另一启动位置为 `0x20000` 时，允许的最大固件为 **124 KiB**；`0x40000` 为 252 KiB，`0x80000` 为 508 KiB。
- 成功后将旧固件启动标记清零、激活新固件并复位；新固件初始化 OTA 模块时会清理旧固件。

因此必须保留 Telink OTA 传输协议，改造单 App 写入后端及完成流程：Bootloader 启动标记常驻；只覆盖 App 槽；写入开始先使 App 状态无效；全部校验后再提交；下载中断后仍进入 Bootloader；按 TL3238 的 XIP/RAM/中断/栈要求交接到 App。现有 SDK 只公开初始化、目标多启动地址、进度/结果回调等接口；`ota_server.c` 未包含在本次 SDK 中，其 `mcu_boot.h` 注明源码需通过原厂授权获取。不能把访问库内私有变量当作已经验证的公共扩展接口。

以实板 **1 MiB Flash** 为上限，完成上述改造后可评估以下布局。**这是候选布局，不是当前板上分区**；结束地址不含。

| 物理范围 | 大小 | 用途 |
| --- | ---: | --- |
| `0x00000..0x20000` | 128 KiB | 原厂 BLE + Telink OTA 常驻 Bootloader |
| `0x20000..0xE0000` | 768 KiB | 单 App |
| `0xE0000..0xE1000` | 4 KiB | App 长度、校验、有效状态 |
| `0xE1000..0xF0000` | 60 KiB | LittleFS、NimBLE 绑定 |
| `0xF0000..0xF1000` | 4 KiB | Bootloader 控制记录 |
| `0xF1000..0xFE000` | 52 KiB | SDK 保留 |
| `0xFE000..0xFF000` | 4 KiB | 校准保留 |
| `0xFF000..0x100000` | 4 KiB | MAC 保留 |

这会将当前 `0x71000..0x80000` 的 LFS 纳入新 App 范围，所以启用前必须迁移现有绑定数据，不能直接按表擦写。首次交接验证也可以暂用当前 B 槽 `0x80000..0xF0000` 作为单 App，保持 LFS 原址，确认功能后再扩容。

P25Q80SU 独立锁方面，Bootloader 的 128 KiB 边界与中间 64 KiB 锁边界对齐，写 App 时可保持 Bootloader 锁定。候选 App 元数据与 LFS 同属 `0xE0000..0xF0000` 的一个 64 KiB 锁块，写 LFS 时仍需软件白名单保护元数据。原厂 `flash_lock/unlock_mid146085()` 会清 WPS，且示例在 OTA 写入时全局解锁，不能直接用于此保护策略。完整依据见 [Flash 型号、当前分区及写保护复核](../tl3238/README.md)。

## 重建与产物

使用仓库 `component/` 中的依赖和已安装的 Telink 工具链；无需原 SDK checkout：

```sh
make -C project tl3238_bootloader
# 只编译建议的起点：
make -C project tl3238_bootloader PROFILE=lean-smp-board-os
# 自定义依赖/输出目录：
python3 project/tl3238_bootloader/build.py --help
```

构建脚本不连接串口、不烧录、不改原 SDK。每种配置输出在 `build/tl3238_bootloader/<profile>/`：

- `firmware.bin/.elf/.map`：镜像、可执行文件和链接表。
- `memory.json`：体积、RAM、SHA-256、镜像检查结果及明确的未烧录状态。
- `build-manifest.json`、`link-command.json`：源文件/库哈希、配置和完整链接参数。
- `compile.log`、`link.log`、`check-fw.log`、`symbols.txt`：验证依据。
- 汇总：[comparison.json](../../build/tl3238_bootloader/comparison.json)。

这些带默认 A/B 擦写语义的 BIN 不能直接作为现有分区上的常驻 Bootloader 使用。本轮只完成“小型原厂 BLE + Telink OTA”体积可行性验证。
