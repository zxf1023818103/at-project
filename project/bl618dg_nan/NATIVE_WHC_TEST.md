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

现有证据优先指向 BL618DG 的收包处理/TCP 交互路径，需要进一步分解
缓冲分配、拷贝、IP 任务处理和 ACK 等待才能确定根因。
固件 SHA-256、完整计数和日志位置见 [native_whc_validation.json](native_whc_validation.json)。

保持手机位置、双向时长和 RF 环境尽量一致，建议连续三轮，记录 App
接收结果与设备字节数/耗时；串口记录在 `build/native-whc/`。
