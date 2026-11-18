# 原生 WHC USB 对照试验

基线提交 `03e12d11692fd287aebdc5776fc667a3a985a628`，试验分支
`test/bl618dg-native-whc-usb`。用户原有 zlib 工作区修改保留，未提交。

本试验按照用户确认的方式：Green2 使用 `~/ameba-rtos` 的原生 WHC USB
固件，BL618DG AP 继续使用同一份 `component/amebagreen2/nan` 协议栈，
手机 App 测 NAN 上传和下载。BL618DG 本地 Wi-Fi 未启用。

## 固件来源和对比范围

原生 SDK 提交为 `9f648d9b9fad552fcbfc7604b58f58b79abb38e9`，配置
`component/wifi/whc/nan_test/module/green2_nan_usb_bt_h4.config`。
它包含该仓库已有的 NAN 修复，并非未经修改的 Realtek 发布包。
本次不修改 SDK 的受跟踪源码；其已有工作区改动保留。构建在项目 `build/`
中的源码链接视图进行，生成的 `build_info.h` 留在该视图中。

共享 NAN 的 `rtw_nan_struct.h` 和 `rtw_nan_vendor_def.h` 与此次原生 SDK
对应头文件逐字节相同。NAN/PASN/SAE、IPv6、TCP 窗口、App 测速协议和
PSRAM 分配策略均沿用基线。原生 USB 直接传输变长 WHC 消息，主机独立
处理 IN/OUT；第一版使用一个 OUT 端点，未实现 Linux 驱动的多 URB 队列。
因此结果只代表这套 MCU 主机实现，不能当作原版 Linux 主机的性能上限。

Green2 原生固件的 Wi-Fi 运行在 KM4TZ，而此前桥接固件通过 AP/NP IPC
访问无线侧；这也属于本次固件替换的差异，不能把所有差值归因于 USB。

## 构建

```sh
python3 project/bl618dg_nan/build_native_green2.py
make -C project/bl618dg_nan NATIVE_WHC=1 whole
# 默认模式仍构建原有 16 KiB USB 桥接版本：
make -C project/bl618dg_nan NATIVE_WHC=0 all
```

启用原生 SDK 的 2 KiB USB 接收缓冲实验（使用独立输出目录）：

```sh
python3 project/bl618dg_nan/build_native_green2.py --bulk-2048 \
  --out build/native-whc/bulk2048
```

脚本只在输出目录生成配置覆盖，检查生成的
`CONFIG_WHC_USB_BULK_2048=y` 后才打包固件，不修改原 SDK 配置文件。
此开关扩大 Green2 的接收缓冲及 SKB 容量，端点 MPS 仍是 512 字节，
并不启用报文聚合。BL618DG 原有 IN URB 已为 2048 字节，OUT 报文上限
保持 1536 字节；当前以太网帧和 WHC 头可容纳在该上限内。

原生输出 `build/native-whc/green2-native.bin`（应用偏移 `0x40000`）和
`build/native-whc/bl618dg-host/whole_flash_data.bin`。
基线输出的不可覆盖副本位于 `build/native-whc/baseline/`。
Green2 刷写使用现有脚本增加的 `--app-offset 0x40000`，保持
`0xe000..0xffff` 校准/系统扇区不变，逐字节读回校验。
BL618DG 全镜像位于地址 0；写非零地址必须用厂商工具的
`--flash --write --start <offset> --file <file>`，不要用 `--firmware --addr`
指定非零 Flash 偏移，该组合会采用默认起始地址 0。

## 配对迁移和恢复

原生 Green2 固件没有自定义桥的 LittleFS RPC，因此配对记录迁到 BL618DG。
主机使用 SDK 默认 media 分区开头 `0x378000..0x379fff` 两个 4 KiB 扇区，
记录带长度、版本序号、CRC32，最后写入提交标志。未知非空内容不自动擦除。
该保留区仅适用于本试验，不可与 media 文件系统共用。

从 Green2 新鲜的 4 MiB Flash 备份只读提取 `/nan-pairing`，保留原有
888 字节身份记录和 App 配对。提取工具为 `extract_pairing.c`，生成的
记录及 Flash 备份仅留在被忽略的 `build/` 中，不提交到 Git，也不打印密钥。
初始化必须先导入原有配对，再启动主机，否则会生成新的身份。

完整 Green2 备份：`build/native-whc/baseline/com15-full-4m.bin`。
原有桥接固件的文件系统位于 `0x300000..0x3effff`；原生 SDK 的文件系统
位于 `0x380000..0x3fffff`。当前原生启动日志报告 LittleFS/蓝牙 FTL 挂载
失败，USB、Wi-Fi、NAN 发布正常；此次没有格式化这一区域。NAN 配对由
BL618DG 保存。恢复基线时使用基线固件及备份的文件系统，避免将两种
文件系统几何混用。迁回后 App 若新增配对，需另行导出主机新记录。

## 已完成检查与测速口径

- 两端编译、烧录校验；原桥接主机编译回归通过。
- 原生 USB High-Speed 枚举，Wi-Fi/NAN init/start 返回 0；NMI 仍为
  `94:cb:60:59:06:fe`；已加载原配对，发布 `_flashstudio._tcp`，监听 TCP 8000。
- 2 MiB PSRAM 读写/Cache、8 轮分配压力及 64 位测速 JSON 板端自检通过。
- 原生 WHC 报文测试涵盖全部以太网长度、DMA padding、截断、非法长度及
  100000 个变异报文；ASan/UBSan 通过。
- 配对存储以真实实现进行 NOR 模拟测试：重启读取、跳过相同写入、三个
  写入阶段断电、CRC 损坏回退和未知数据保留均通过。

存储检查命令：

```sh
cc -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Iproject/bl618dg_nan/tests/stubs -Icomponent/cherryusb_config/whc_usb \
  project/bl618dg_nan/tests/store_test.c component/cherryusb_config/whc_usb/whc_wire.c \
  -o build/native-whc/store-test
ASAN_OPTIONS=detect_leaks=0 build/native-whc/store-test
```

2026-11-16 手机实测（上传为设备接收计数，下载为设备提交计数）：

| 路径 | 上传 Mbps | 下载 Mbps |
|---|---:|---:|
| 基线 16 KiB 桥接 | 9.562 | 19.542 |
| 原生 WHC USB 第 1 轮 | 10.203 | 12.824 |
| 原生 WHC USB 第 2 轮 | 10.033 | 12.790 |
| 原生 WHC USB 诊断轮 | 9.609 | 12.334 |

基线上一次手机测试：设备接收上传 `12058624 bytes / 10088 ms`；设备提交
下载 `24444928 bytes / 10007 ms`。原生第 1 轮为上传 `12910592 / 10122 ms`，
下载 `16056320 / 10016 ms`；第 2 轮为上传 `12648448 / 10085 ms`，下载
`15990784 / 10002 ms`。两轮均无协议错误、软件收发丢包、USB 无效帧或
I/O 错误，另完成 100 次 PONG。用户确认重新配对成功；主机诊断镜像重启
后，保存的新配对通过 PASN type=2 认证，无需再次重配。

下载计数为 `submitted`，不等同于手机接收计数；尚未收到 App 显示数值。
不能从 USB 原始吞吐量推算 NAN 结果，也不能据此断定芯片硬件性能上限。

## BL618DG 诊断轮

`make -C project/bl618dg_nan NATIVE_WHC=1 USB_DIAG=1 whole` 输出到独立的
`build/native-whc/bl618dg-diag/`，启用已有共享 TCP 元数据诊断及 USB 耗时
计数；不修改 Green2 固件、TCP 窗口或 USB 等待策略。

该轮上传 `12124160 / 10094 ms`，下载 `15466496 / 10031 ms`，软件丢包和
USB 错误均为 0。主机状态统计：

- 35381 次中断完成、139 次轮询补获完成，约 99.6% 正常经中断完成。
  因此 1 ms 补查不是本轮主要瓶颈。104879 次轮询检查还包含设备空闲等待，
  不能当作数据传输失败次数。
- OUT 平均 424 us；RX 回调平均 481 us。RX 回调包含唤醒优先级更高的
  IP 任务后发生的抢占，并非纯 USB 拷贝耗时，也不等同于该任务 CPU 时间。
- 上传 11846 个 TCP 入包只触发 655 个 TCP 出包，主要为 ACK。前 256 个
  报文元数据中 ACK 常相隔 14–33 ms，也存在更长无线/协议停顿。
- 共享 FreeRTOS+TCP 的完整 MSS 延迟 ACK 默认值为 20 ms；当前核心以
  `-Os` 编译。下载 socket send 累计等待约 8760 ms，这包含发送缓冲背压，
  不能直接当作 CPU 耗时。

该轮证据指向 BL618DG 的收包处理/TCP 交互路径，后续分段诊断和修复如下。
固件 SHA-256、完整计数和日志位置见 [native_whc_validation.json](native_whc_validation.json)。

## 分段诊断与 PSRAM 执行修复

Green2 保持同一份原生固件。BL618DG 的 `USB_DIAG=1` 新增链接包装器，
分别测量接收缓冲分配、拷贝、端点匹配和投递 IP 队列，并通过调度器运行
计数器记录各任务占用。每轮结束后才输出汇总，不在逐包路径打印日志。
投递耗时包含优先级更高的 IP 任务及其他任务抢占，任务计数包含归入当前
任务的中断时间；两者不能作为独立 CPU 开销相加。

| 执行位置 / ACK 设置 | 上传 Mbps | 下载提交 Mbps |
|---|---:|---:|
| Flash，原 ACK | 8.715 | 12.036 |
| Flash，ACK 上限 1 ms | 8.151 | 12.209 |
| PSRAM，原 ACK | 21.269 | 17.411 |
| PSRAM，ACK 上限 1 ms | 17.985 | 16.664 |

同参数的上传接收路径对照（平均微秒）：

| 阶段 | Flash | PSRAM |
|---|---:|---:|
| 分配 | 56 | 8 |
| 拷贝 | 28 | 20 |
| 端点匹配 | 23 | 3 |
| 投递及任务抢占 | 341 | 54 |
| 整个接收回调 | 478 | 90 |

上传 CPU 空闲占比由 33.6% 变为 69.5%。独立微基准将 1100 字节从 USB
非缓存内存拷到 PSRAM，每次约 16 us；拷贝不足以解释原回调的数百微秒。
PSRAM 对照只启用 SDK 的 `CONFIG_PSRAM_COPY_CODE`，链接图确认 TCP、
内核、分配器等从 `0xb000...` 改为 `0xb800...`，同时搬移只读数据。
这确认 Flash 执行/访问路径是显著开销，尚未进一步区分指令缓存缺失与
只读数据访问成本，不代表已经确定芯片或无线链路的最终速度上限。

1 ms ACK 在两种执行位置都未提高上传速度。Flash 上传 TCP 出包由 614
增至 10367，PSRAM 下由 1350 增至 22110，多数为 ACK。因此正式配置保留
共享栈的原 ACK 策略，仅将 `CONFIG_PSRAM_COPY_CODE=y` 设为本工程默认值。
两种执行位置的各两轮测试结束后，USB I/O 错误、非法帧、软件丢包及协议
错误均为 0。PSRAM 代码约占 280 KiB，仍保留约 3.9 MB PSRAM 空闲；
USB DMA 缓冲继续使用片内非缓存区。

复现同一诊断方式（使用独立输出目录，避免覆盖已有测速镜像）：

```sh
make -C project/bl618dg_nan NATIVE_WHC=1 USB_DIAG=1 \
  CONFIG_PSRAM_COPY_CODE=n OUT="$PWD/build/native-whc/repro-flash" whole
make -C project/bl618dg_nan NATIVE_WHC=1 USB_DIAG=1 \
  CONFIG_PSRAM_COPY_CODE=y OUT="$PWD/build/native-whc/repro-psram" whole
```

仅诊断版本提供 `nanack 1` 和 `nanack 0`，分别设定 ACK 超时上限和恢复
上游参数，必须在两次测速之间修改。默认值为 0，每个方向汇总均记录该
值和实际缩短次数。此开关只作用于 TCP 8000 收到数据后的待发送 ACK，
不改上游源码、窗口和缓冲所有权。`USB_DIAG=0` 完全不链接这些包装器。

正式配置的原生 WHC 与原有 USB 桥接两种主机均构建验证；本次硬件实测
使用原生 Green2，桥接版本仅做编译回归。

关闭诊断后的最终固件已烧录 COM37 并通过 SHA-256 校验，App 两轮结果：

| 轮次 | 上传收到字节 / ms | 上传 Mbps | 下载提交字节 / ms | 下载提交 Mbps |
|---|---:|---:|---:|---:|
| 1 | 27459584 / 10073 | 21.808 | 20709376 / 10017 | 16.539 |
| 2 | 22478848 / 10055 | 17.884 | 20578304 / 10030 | 16.413 |

两轮结束后 `up=2 down=2 err=0 rxdrop=0 txdrop=0`，USB
`drop=0 bad=0 io=0`。PSRAM 剩余 3886796 字节，OCRAM 剩余 151100 字节。
2 MiB PSRAM/Cache、8 轮分配压力与 64 位 JSON 自检通过，重启后复用
原配对完成 PASN type=2 认证。设备保留运行该版本，ACK 使用上游默认值。
App 已确认两轮完成，但未提供显示值，下载仍以设备 `submitted` 口径记录。

最终镜像为 `build/native-whc/bl618dg-psram-final/whole_flash_data.bin`，
SHA-256 `fbe696d0e791570cf38110b66e9a455f3811c27f1f1c7d051ab444a66a92a758`。
完整结果见 `native_whc_validation.json` 的 `rx_path_diagnosis` 与
`psram_execution_fix`。

## Green2 2 KiB Bulk 缓冲对照

在上述 PSRAM 正式主机镜像不变的条件下，启用
`CONFIG_WHC_USB_BULK_2048=y`，用 COM15 烧录、COM19 控制复位。
生成的 Green2 配置逐行比较只有该选项不同，原 SDK 源码和工作区状态
均保留；固件逐字节读回通过。启动日志确认：

```text
USB bulk RX capacity=2048, SKB capacity=2144, reserve=64
```

之前对应值为 `1536 / 1632 / 64`。BL618DG 只复位重连，未更换镜像，
继续使用 `fbe696d0...a92a758` 的 PSRAM 代码、原 ACK 参数及关闭的诊断。
USB High-Speed 枚举、NAN 发布正常，复用配对完成 PASN type=2 认证。
App 确认两轮完成，板端计数如下：

| 轮次 | 上传收到字节 / ms | 上传 Mbps | 下载提交字节 / ms | 下载提交 Mbps |
|---|---:|---:|---:|---:|
| 1 | 28573696 / 10053 | 22.738 | 20643840 / 10007 | 16.503 |
| 2 | 28508160 / 10082 | 22.621 | 19857408 / 10011 | 15.868 |

两轮后 `up=2 down=2 err=0 rxdrop=0 txdrop=0`，USB
`drop=0 bad=0 io=0`。上传高于此前两个样本，下载未见明确收益；由于样本
少且存在无线波动，不能据此认定增大缓冲必然带来这些速度差异。
该选项仅增加接收容量，不会将当前约一个以太网帧的报文聚合到 2 KiB，
端点 MPS、主机发送报文上限及实际传输长度策略保持原样。

Green2 当前保留 2 KiB 缓冲版本，镜像
`build/native-whc/bulk2048/green2-native.bin` 的 SHA-256 为
`4fc3b0e30967188379fb232fc5d5f2846685c1a72b4643ce086fc66b520c483f`。
原 1536 字节版本仍保存于 `build/native-whc/green2-native.bin`。
完整结果、固件指纹和日志路径见 `native_whc_validation.json` 的
`bulk_2048_experiment`。

## SRAM 热点与零 PSRAM 对照

先将截至 `e086a26` 的测试分支推送到 `origin/test/bl618dg-native-whc-usb`，
再进行本轮实验。Green2 保持上节 2 KiB 固件及相同 SHA-256。

主机使用 `PSRAM=0 SRAM_HOT=1`，关闭 PSRAM 驱动、代码复制和 PSRAM XIP，
PSRAM 区域长度为 0。AP 独占 448 KiB OCRAM 和 320 KiB WRAM，NP 不启用。
`app/nan_memory.cmake` 在构建目录生成 SDK 链接模板的视图，把已测得的
接收、IP/TCP、分配及调度路径放入 SDK 启动时加载的 `tcm_text/tcm_rodata`。
包和协议缓冲使用 WRAM 缓存别名，DMA 仍使用原片内非缓存区；共享协议、
ACK、MSS、窗口和队列容量不改。硬件自检验证 WRAM 缓存别名的读写与维护。

初版诊断轮上传 27525120 bytes / 10047 ms（21.917 Mbps），下载提交
20905984 bytes / 10036 ms（16.664 Mbps）。上传回调平均 72 us，其中分配
3 us、拷贝 18 us、匹配 7 us、投递及任务抢占 39 us；CPU 空闲 75.4%。
这比此前 PSRAM 诊断的整个回调 90 us 更低。继续检查调用路径发现
`FreeRTOS_MatchingEndpoint` 调用的 `memcmp` 仍位于 Flash，正式版将它及
`memmove` 也链接到 SRAM。上述 72 us 属于初版诊断镜像，未当作最终版
逐函数测量值；本次采用分段计时及调用路径定位，并非 PC 采样排名。

最终 ELF 核验的部分函数地址：

| 函数 | SRAM 地址 |
|---|---|
| `green2_nan_rx` | `0xa10170fe` |
| `prvIPTask` | `0xa1008d56` |
| `xProcessReceivedTCPPacket` | `0xa100b21a` |
| `FreeRTOS_MatchingEndpoint` | `0xa100fb34` |
| `pxGetNetworkBufferWithDescriptor` | `0xa10109fc` |
| `kmalloc` | `0xa1010e6c` |
| `vTaskSwitchContext` | `0xa1005e3a` |
| `usbh_submit_urb` | `0xa1015310` |
| `memcmp` | `0xa1017cc8` |

热点代码共 84948 字节，相关只读数据 16018 字节。其余代码例如
`green2_pasn_handle_auth_pasn_1` 仍位于 Flash（`0xb002f5f8`）。所有非空
ELF LOAD 段均不在 PSRAM，未链接 `board_psram_x8_init`，运行始终报告
`PSRAM=0/0`。256 KiB 两模式读写/Cache、8 轮分配压力和 JSON 自检通过。

关闭诊断后的正式镜像在 COM37 烧录并通过 SHA-256 验证，完整速率记录：

| 轮次 | 上传收到字节 / ms | 上传 Mbps | 下载提交字节 / ms | 下载提交 Mbps |
|---|---:|---:|---:|---:|
| 第 1 轮 | 27459584 / 10037 | 21.886 | 20578304 / 10051 | 16.379 |
| 补测（第 3 轮） | 28770304 / 10047 | 22.908 | 21299200 / 10019 | 17.007 |

第二轮的速率行在切换串口记录期间遗漏，未推算或计入速度对比；随后补测
一轮。最终计数 `up=3 down=3 pong=100 err=0 rxdrop=0 txdrop=0`，USB
`drop=0 bad=0 io=0`。OCRAM 剩余 179724 字节，WRAM 剩余 305704 字节。
复用原配对完成 PASN type=2 认证。结果说明当前 NAN 单连接双向测速可在
不使用 PSRAM 时达到相近速度，不等同于任意并发或缓冲压力均已验证。

原生 SRAM、原生 PSRAM 和桥接 SRAM 三种配置构建通过；后两种在本轮仅
做编译回归。最终镜像保留运行于 COM37，文件
`build/native-whc/bl618dg-sram-final/whole_flash_data.bin`，SHA-256：
`871ed73e7d053d3bde825b08fdae4541e485447a8d44d7abd3fef915bf5efcd2`。
完整计时、地址、固件和日志见 `native_whc_validation.json` 的
`sram_only_experiment`。

保持手机位置、双向时长和 RF 环境尽量一致，建议连续三轮，记录 App
接收结果与设备字节数/耗时；串口记录在 `build/native-whc/`。
