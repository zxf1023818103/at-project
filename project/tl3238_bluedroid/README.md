# TL3238 Bluedroid 验证工程

独立选择 `component/bluedroid_config` 的 ESP-IDF Bluedroid Host，连接
`component/telink_sdk` 内的普通单连接 LE Controller。编译输入全部来自仓库
`component/`，不引用外部 Telink SDK。原 `project/tl3238` 仍使用 NimBLE。

## 构建与烧录

```sh
./project/tl3238_bluedroid/build.sh
```

输出 `build/tl3238_bluedroid/firmware/tl3238_bluedroid.{elf,bin,map}`。
烧录使用 `project/tl3238_hello/flash.ps1`，传入上述 BIN 和
`-SkipSerialVerify`（Hello 工程的默认检查不适用）。链接器限制镜像不超过
448 KiB；仅写 A 分区。UART0：PA1 TX、PA2 RX，COM19，115200 8N1。

切换 Host 前先用 `project/tl3238/sws_read.ps1` 备份完整 1 MiB Flash。
备份包含绑定密钥，应放在受限访问目录中。

在 Windows Python（安装 pyserial、bleak、cbor2）运行：

```powershell
python project/tl3238_bluedroid/verify.py --flash
```

脚本检查 SWS 回读、CLI、FreeRTOS 队列/FPU/定时器、tickless、堆及任务栈，
并执行三次 BLE 连接，每次写入、通知回显、读取核对 32 个不同长度的数据包。
`--cli-only` 跳过无线测试，`--rounds`、`--cycles` 调整次数。
结果保存在 `build/tl3238_bluedroid/verification.{json,log}`。

## BLE 服务

广播名 `TL3238-Bluedroid`，复用板卡原 MAC。

| 功能 | UUID / 参数 |
| --- | --- |
| 共用 CoC 服务 | `b1f01001-97b3-4e38-baf4-91afb3cd92f0` |
| PSM 读取特征 | `FFF4`，值 `81 00`，PSM 129 |
| CoC 参数 | MTU 2048，MPS 247，初始 credits 24 |
| GATT 回显服务 | `b1f01002-97b3-4e38-baf4-91afb3cd92f0` |
| 回显特征 | `FFF5`，读/写/通知，最大 244 字节 |

CoC 使用 `component/ble_coc_config` 的 Bluedroid 后端和共用测速协议，
可使用 Flash Studio iOS 的 LE CoC 测速页。PC 脚本验证 PSM 发现和 GATT
数据传输，不把这些结果作为 CoC 吞吐量结果。

## 平台适配与范围

- `component/telink_sdk_config/bluedroid_port` 提供 H4、FreeRTOS 堆、
  ESP 定时器、UART 日志和硬件 TRNG；NVS 使用全平台共用的 LFS 实现。
  通过构建目录中的 Host 配置覆层关闭 Controller 不支持的 HCI C2H 流控
  （`0x0C31`、`0x0C33`）；保留共享 Bluedroid 上游及其他平台的默认配置。
- Heap_5 分别使用 38 KiB IRAM 和 38 KiB DRAM；启动时在首次分配前登记。
  HCI 任务栈增至 4 KiB，以容纳本工具链的格式化函数。CLI `heap`、`tasks`
  显示实际余量，`ble` 显示 Host/Controller 与回显计数。
- NVS 使用 `component/bluedroid_config/nvs_lfs.c` 和
  `component/ble_store_config`，复用 0x71000–0x80000 的 LittleFS 分区。
  两套 Host 共用 `/ble.bonds`，首次导入原 NimBLE 密钥和旧 Bluedroid 绑定；
  原 `/nimble.bonds` 作为回退备份保留。迁移逻辑位于共用组件，无芯片分支。
  回显特征的 CCCD 仍按连接管理，客户端每次连接后重新订阅。
- 这是 LE 单连接验证工程，尚未移植 CS/RAS、距离解算、mcumgr/OTA，
  不提供 BR/EDR。SMP 使用上游原生密码实现。
- 两套 Host 都使用出厂地址 `60:3F:FB:70:E5:D3` 和同一份本地 IRK/绑定。
  切换后可用已有 LTK 加密，不再使用独立随机地址隔离 Host。
- 回显服务用于无加密链路的功能验证；长写、多个连接及 CoC 实机吞吐量需
  分别验证。FFF6 是需要加密的只读探针；`verify.py --pair --storage-write`
  验证绑定与 NVS 写入，复位后使用 `--pair --storage-check` 验证恢复。
  `storage` 显示错误/提交计数。新 Host 与旧 Host 的服务表不同，客户端应重新发现 GATT 服务。

恢复原固件时通过 SWS 烧录备份对应的原 NimBLE A 镜像，并保留 LFS、B 和
校准/MAC 分区。不要用全片擦除进行 Host 切换。

## 2026-11-14 实测

板卡 TL3238，PUYA 1 MiB Flash，MAC `60:3F:FB:70:E5:D3`，Windows BLE
适配器。最终镜像 SHA-256：
`9ab479bb9f02d2d0b67023803598ff6f25028b513b43bbb625c0e9d0c8d666d9`。

| 项目 | 结果 |
| --- | --- |
| BIN | 397,282 字节（387.97 KiB），SWS 烧录并逐字节回读一致 |
| 完整回归 | 226 项检查通过；3 次连接，96 组写入/通知回显/读取一致 |
| GATT | MTU 247；数据长度 1 / 20 / 100 / 244 字节；CCCD 启停通过 |
| 复位后 | 再次连接，16 组回显通过；广播自动恢复 |
| 堆 | 总量 77,824 B；三轮测试后空闲 24,368 B，最低 19,424 B |
| 栈 | 三轮后 HCI 余 1,536 B、Controller 余 1,760 B；全部任务至少 528 B |
| RTOS/UART | FPU、队列顺序、软件定时器、tickless、堆回收检查通过；错误计数为 0 |
| 分区保留 | A 外全部 589,824 B 与备份一致，包含 60 KiB LFS、B、保留区、校准、MAC |
| 编译回归 | 原 NimBLE 完整工程、Bouffalo Bluedroid 静态组件均通过 |
| 共用 CoC 主机测试 | 协议测试和两套 NimBLE / 两种配置的 ASan、UBSan 测试通过 |
| 依赖审计 | 1,168 个构建输入，无外部 Telink SDK 引用 |

最终记录：[实机检查](../../build/tl3238_bluedroid/verification-final.json)、
[复位复测](../../build/tl3238_bluedroid/verification-after-reset.json)、
[分区核对](../../build/tl3238_bluedroid/partition-check.json)、
[依赖审计](../../build/tl3238_bluedroid/dependency-audit.json)。
首轮 Windows 复用旧 `BLEDevice` 时第二次连接取消；验证脚本已改为每次断开后
重新扫描获取设备，最终三轮全部通过。板卡没有天线，本次 RSSI 约 −87 dBm；
以上是功能验证，不是无线距离、长期稳定性或 CoC 吞吐量结论。

后续共用持久化验证已完成：NimBLE 旧密钥迁入平台无关的 `/ble.bonds`，两套
Host 使用同一身份并加密重连；最终回读确认旧密钥及备份文件未变。Bluedroid
与 iPhone 13 Pro 双向各 60 秒 CoC 通过，手机→板端 0.455 Mbps、板端→手机
0.265 Mbps，双方字节计数一致、错误为 0。该轮使用显式 PSM 129，不代表跨
Host 的 GATT 缓存兼容已解决；无天线条件下的早期超时也记录在报告中。
后续最终固件大小、SHA 和测试记录见
[共用持久化验证报告](../../build/ble-store-validation/unified-validation-summary.json)，
以上早期回归表保留对应当时构建的结果。
