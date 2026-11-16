# BL618DG AP FreeRTOS NAN，通过 USB 使用 Green2 无线侧

BL618DG 的 AP 核运行 NAN 控制、hostap PASN/SAE 配对、mbedTLS、FreeRTOS+TCP
IPv6 和 App 的 `NAN-THROUGHPUT/1` 服务。Green2 NP 运行原有 NAN 无线驱动；
Green2 AP 将 USB 命令、事件和以太网帧转到 WHC IPC，不运行另一份认证或 TCP
测速服务。BL618DG 的 NP 和本地 Wi-Fi/蓝牙未启用。

两种部署直接编译同一份 `component/amebagreen2/nan` 源码，通过 `host.cmake`
选择传输适配。协议栈没有复制成 BL618DG 专用版本。`nan_platform.h` 抽象无线
启用、发送和状态；Green2 使用本地 IPC，BL618DG 的 `app/nan_port.c` 使用
`component/cherryusb_config/nan_usb`。原有 Green2 `NAN=ON` 独立模式保留。

## 启动与 App

- COM37：BL618DG 烧录/控制台，500000 baud。
- COM15：Green2 烧录/控制台，1500000 baud；COM19：Green2 BOOT/RESET。
- 两块模组通过原生 USB 直连，BL618DG 为 USB Host，Green2 为 Device。
- App 服务名仍为 `_flashstudio._tcp`，显示名为 **BL618DG FreeRTOS**。
- 初次配对 PIN 为原实现的开发值 **123456**。TCP 端口 **8000**，支持
  `NAN-PING` 和原有双向吞吐量协议，App 无需修改。

配对身份和密钥记录继续保存在 Green2 的 `/nan-pairing`，由 BL618DG 的共享
PASN 代码经 USB 调用原有原子读写接口。正常 firmware-only 烧录保留此文件。
密码学计算和配对授权在 BL618DG AP 完成，Green2 桥只转发命令和提供存储。
不要把配对文件或含凭据的原始数据提交到仓库。

COM37 命令：`nan status`、`nan start`、`nan stop`、`reboot`。
COM15 的 `nan status` 显示无线桥及 USB 队列状态。
**COM37 必须保持 pyserial `DTR=True, RTS=False` 的正常启动电平。**
`DTR=False` 时读日志看似正常，但执行 `reboot` 会进入 BootROM 下载模式。
Windows 侧可用本工程的控制台脚本记录启动和运行状态：

```powershell
python project/bl618dg_nan/console.py --reset --seconds 15
python project/bl618dg_nan/console.py --command reboot --seconds 15
python project/bl618dg_nan/console.py --seconds 300 --log build/nan-usb/app-test.log
```

首次启动会验证两端结构 ABI，并完成 12 次不同长度的 USB 数据往返校验。
看到以下状态后再打开 App：

```text
[NAN] PSRAM 2 MiB read/write/cache test PASS ...
[NAN] PSRAM allocation stress PASS: 8 rounds, 2x64KiB + 32 network buffers + 64 MTU blocks
[NAN] 64-bit + upload/download JSON self-test PASS (synthetic I/O)
[NAN-USB] ABI + 12 payload roundtrips PASS
[NAN] published _flashstudio._tcp / BL618DG FreeRTOS
[NAN] standalone startup result=0
[NAN] FreeRTOS TCP port 8000 ready (PING + NAN-THROUGHPUT/1)
```

App 建立 NDP 前，`nan status` 中 `link=0` 是正常等待状态。连接后应变为
`link=1`，随后打印 TCP 接入和 upload/download 字节数、耗时及速率。
USB 异常会停止交换并打印错误，重新接好线后使用 COM37 `reboot` 重建会话。
目前验证范围为固定连接和 AP 重启，不宣称任意热插拔自动恢复。

## PSRAM 与构建

`CONFIG_PSRAM=y`，AP 分配 4 MiB PSRAM（NP 不使用）。本工程显式将 libc /
mbedTLS 的普通分配优先级设为 PSRAM，并为 NAN、FreeRTOS+TCP 及 USB 消息
缓冲选择 `app/nan_heap.c` 的 PSRAM 分配接口。SDK 的 `pvPortMalloc` 本身固定
使用 OCRAM，单独启用 PSRAM 不能改变该路径；任务栈、内核对象和 CherryUSB
驱动仍使用原来的分配方式。`nan status` 同时显示各堆剩余空间和最大空闲块。

启动时在 PSRAM 专用堆分配 2 MiB，执行两种地址相关模式的
写入、Cache clean/invalidate 和逐字读回。USB DMA 帧及 EHCI 描述符仍在片内
非缓存区。P-256 采用共享 mbedTLS 软件后端，未调用 Green2 的 ROM PKE。
随后完成 8 轮分配压力测试：同时持有两个 64 KiB 数据块、32 个真实 TCP/IP
网络缓冲和 64 个 MTU 大小的数据块，确认全部位于 PSRAM、数据正确且释放
后无泄漏，再启动无线服务。该用量超过整个 OCRAM 堆，覆盖下载阶段的缓冲
并存场景。

```sh
# 在仓库根目录执行，先构建/烧录 Green2 Device，再烧录 BL618DG Host。
make -C project/amebagreen2 NAN_USB=ON MINIMAL=ON \
  BUILD="$PWD/build/nan-usb/green2-radio" FW_VERSION=nan-usb-radio APP_BUILD=10
make -C project/bl618dg_nan whole -j8
```

输出：`build/nan-usb/green2-radio/firmware.bin` 和
`build/nan-usb/bl618dg-host/whole_flash_data.bin`。
Green2 沿用 `project/amebagreen2/flash_uart.py --port COM15 --boot-port COM19`。
BL618DG 必须使用本 SDK 的 `BLFlashCommand.exe --chipname bl618dg --port COM37
--baudrate 500000 --firmware <镜像> --addr 0x0 --reset`。

`CONFIG_VSNPRINTF_LONG_LONG=y` 是测速协议的必需配置：RV32 的 `PRIu64` 使用
`%llu`，SDK 默认关闭此支持时会漏掉数值，导致 App 报告数据格式错误。
启动自检使用板端实际 libc 和共享 `nan_tcp_protocol.c`，校验超过 32 位及
`UINT64_MAX` 的格式化，并验证上传、下载的帧、字节数和完整 JSON。
自检使用内存流及模拟时钟，其速率不是无线吞吐量。

此工程使用 CMake 3.18+。Bouffalo 将 CMake `project()` 重定义为生成固件的宏，
`mbedtls_sdk.cmake` 建立仅存在于构建目录的源码链接视图，让 Mbed TLS 调用
原 CMake 命令。SDK 与 mbedTLS 子模块均不修改。BL618DG EHCI 沿用已实测的
同步完成状态补查，USB 传输开销包含在 App 实际测得的吞吐量中。

## 检查

```sh
cmake -S component/amebagreen2/nan/tests -B build/nan-usb/native-tests \
  -G Ninja -DNAN_TEST_SANITIZERS=ON
cmake --build build/nan-usb/native-tests -j8
ctest --test-dir build/nan-usb/native-tests --output-on-failure
```

包括 crypto/SAE、PASN 握手、NDP 解析、TCP 测速协议、早期对端状态和 USB
聚合帧边界测试。LeakSanitizer 需在无 ptrace 的正常进程环境运行。
构建、烧录及串口记录保存在 `build/nan-usb/`，手机 App 的实际吞吐量需要
用户启动测试后读取，不能由 USB 原始吞吐量推算。

2026-11-16 板端验证通过：4 MiB AP PSRAM 堆启用、2 MiB 两模式读写与 Cache
自检通过，USB High-Speed 枚举和 12 次数据往返通过，服务发布与 TCP 8000
监听就绪；保持正确 DTR 电平后的硬件复位及 SDK `reboot` 均恢复服务。
共享 NAN 原生测试（ASan/UBSan/LeakSanitizer）6/6 通过，Green2 独立
`NAN=ON` 和 USB 桥两种镜像均编译成功。固件大小、SHA-256 和记录路径见
[validation.json](validation.json)。随后手机已完成两次上传，但 App 解析结果
失败；已定位为 SDK 默认关闭 `%llu` 支持，启用配置后重新烧录 COM37。
板端两方向完整 JSON 自检及主机 JSON 解析均通过。随后手机上传完成
11,927,552 bytes / 10.132 s（9.417 Mbps），下载准备阶段触发了 SDK OCRAM
分配失败。已将数据缓冲分配改为 PSRAM 并烧录，8 轮板端分配压力测试、JSON
回归和 USB/NAN 启动均通过。

此修复后的 App 重试，设备完成两方向：接收上传 12,058,624 bytes / 10.088 s
（9.562 Mbps）；提交下载 24,444,928 bytes / 10.007 s（19.542 Mbps）。协商
MSS 为 1024，软件 RX/TX 丢包计数均为 0。下载数值来自设备提交到 TCP 的
计时，尚未取得手机接收计数作一致性核对。随后 100 次 PONG 正常，无内存
异常；具体数据见 `validation.json` 的 `app_run_after_psram_fix`。
