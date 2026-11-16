# BL618DG / AmebaGreen2 CherryUSB 实机吞吐量

测试日期：2026-11-16，设备通过原生 USB 线直连，两种主从角色均完成测试。
以下为 **USB Bulk 有效载荷吞吐量**，包含固件处理及全量数据校验开销。
两个方向分别运行，不是同时双向传输，也不是 NCM/TCP 网络测速。

## 结果

16 KiB 分块，每个方向每轮 64 MiB，连续三轮，`verify=1`。
平均值按三轮总字节数除以总耗时计算，Mbps 使用十进制。

| USB 主机 | USB 从机 | 数据方向 | 平均 Mbps | 三轮范围 Mbps | 校验 |
|---|---|---|---:|---:|---|
| BL618DG | Green2 | BL618DG → Green2 | 174.763 | 174.762–174.762 | PASS |
| BL618DG | Green2 | Green2 → BL618DG | 187.259 | 187.258–187.258 | PASS |
| Green2 | BL618DG | Green2 → BL618DG | 148.745 | 148.717–148.758 | PASS |
| Green2 | BL618DG | BL618DG → Green2 | 156.082 | 156.067–156.112 | PASS |

固件每轮输出的 `kbps` 向下取整，因此区间端点与总时间计算的均值末位
可能略有差异。定时器分辨率为 1 ms。

另外完成了小分块测试，每项每方向 1 MiB、一次：

| 主机 | 分块 | 主机 → 从机 Mbps | 从机 → 主机 Mbps | 校验 |
|---|---:|---:|---:|---|
| BL618DG | 512 B | 97.541 | 83.055 | PASS |
| BL618DG | 4096 B | 158.275 | 174.762 | PASS |
| Green2 | 512 B | 161.319 | 114.912 | PASS |
| Green2 | 4096 B | 158.275 | 152.520 | PASS |

两组正式记录合计 20 个方向测试、776 MiB 有效载荷。
全部以 High-Speed 枚举（`speed=3`，链路标称 480 Mbps），主从字节数及
包数一致，数据校验错误和 I/O 错误均为零。每个包验证 magic、序号、偏移、
长度及其余每个 32 位数据字。最终结果须通过从机 EP0 状态读回确认。

## 设备与固件

| 设备 | 标识 | 串口 | USB 控制器 |
|---|---|---|---|
| BL618DG | chip ID `30b63c42e8b4`，4 MiB Flash | COM37 烧录/控制，控制台 500000 baud | EHCI 主机 / 原生 DMA 从机 |
| AmebaGreen2 RTL8711F B-cut | MAC `94:c9:60:59:06:fe`，4 MiB Flash | COM15 烧录/控制台 1500000 baud，COM19 RTS/DTR 控制 | DWC2 `4f54400a`，UTMI 16-bit |

BL618DG 使用 AP 单核测试工程，无无线业务；Green2 使用 `MINIMAL=ON`，
保留已有 Wi-Fi 服务。数据路径使用单个 16 KiB 非缓存 DMA 缓冲区和同步
Bulk URB，接收端在传输路径内验证数据。没有使用多 URB 流水线或关闭校验
来测峰值，因此不代表 USB 控制器的性能上限。

Green2 使用抽取到组件目录的 CherryUSB v1.6.1（上游
`c9625ffa773ad10b8824d1b5361bca2ccc1f3d1e`），PHY 库来自已固定的 Realtek
SDK `4b964de6cbaa08c95d37ff3c1d52805e69f430d3`。构建文件没有引用
`platform/ameba-rtos`。BL618DG 使用本仓库 Bouffalo SDK
`2e1ae43ad20f6e6c5e5bd55ad9f56447e9a994ae` 内的 CherryUSB v1.6.1。

测试修正包括：Green2 主机端点数量配置、控制传输返回长度包含 8 字节
SETUP 的处理、IN/OUT 各自保存 DATA toggle，以及 BL618DG 同步 EHCI
传输完成通知的补查。EHCI 补查以完成的 qTD 为依据，不重发数据；最多每
1 ms 检查一次，其开销包含在计时中。缺失通知的更底层原因尚未确定。
详见 [BL618DG 工程说明](../../../project/bl618dg_usb_bench/README.md)。

四个被测镜像的完整路径、字节数与 SHA-256 保存在以下原始结果中：

- [BL618DG 作主机：10 项结果](results/2026-11-16/bl618dg-host.json)
- [Green2 作主机：10 项结果](results/2026-11-16/amebagreen2-host.json)

烧录均经过验证：BL618DG 比对烧录工具读取的 SHA-256，Green2 进行逐字节
读回比较。测试结束时保留 **BL618DG 主机 + Green2 从机** 固件。

## 复现与记录

构建和测试命令见 [README](README.md)。以下本机记录位于忽略的
`build/usb-throughput/` 目录，相对路径以该目录为基准：

- `bl618dg-host-results-final/uart.log`：最终 BL618DG 主机测试串口日志。
- `green2-host-results-fixed/uart.log`：Green2 主机测试串口日志。
- `flash-bl618dg-host-final-validated.log`、`flash-bl618dg-device.log`：BL618DG 烧录校验。
- `flash-green2-host-fixed.log`、`flash-green2-device-final.log`：Green2 烧录读回校验。
- `build-green2-default.log`：关闭 USB 测试功能的默认 MINIMAL 构建通过。
- `ehci-timeout.log`、`ehci-timeout-console.log`：早期 EHCI 完成通知超时诊断，未计入正式结果。

烧录前已保存两块模组完整 4 MiB Flash：

| 本机备份文件 | SHA-256 |
|---|---|
| `com37-original-4m.bin` | `a6b6589461fc7d1be28c61dde8f576ca69cd7851ad0d5f518f32a7253d9da763` |
| `com15-original-4m.bin` | `983738a4075c9df4f91dfef88a57bcbe22b9986275dfd819dcc69039ad4eb686` |

本次未改变两个 SDK 子模块的已跟踪文件；CherryUSB 上游 29 个抽取文件和
Green2 PHY 库 SHA-256 均已核对。原有 zlib 的 257 个文件及 Git 状态与
任务开始前一致。
