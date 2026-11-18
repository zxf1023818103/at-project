# TL3238 UART hello world

最终引脚按用户更正：**PA1 = UART0 TX，PA2 = UART0 RX**。
串口为 **115200 baud、8 数据位、无校验、1 停止位、无流控**。
上电立即输出 `hello world\r\n`，随后约每秒输出一次。

接线：PA1 → USB 串口 RXD，PA2 ← USB 串口 TXD，GND 共地。
SWS 烧录器 SWM → 芯片 SWS，GND 共地。

## 编译（Linux / WSL）

在仓库根目录执行：

```bash
bash project/tl3238_hello/build.sh
```

需要 Python 3、CMake 和 Ninja；未安装工具链时还需 curl 和 tar/xz：

- 平台驱动、启动和校准代码已提取到 `component/telink_sdk/platform`，来自提交 `e88822226e4702910204e8599f44053065b68432`（V4.1.0），不依赖原 SDK 目录。
- Telink `TL32 ELF MCULIB V5F GCC12.2`，`V53xf_Linux_X64`。

可用环境变量 `TL3238_TOOLCHAIN` 指定编译器根目录，`TL3238_HELLO_BUILD_DIR` 指定构建目录。默认使用新的 `build/tl3238_hello/component-firmware/`，避免与早期 SDK CMake 缓存混用。

工程使用自己的 CMake 入口和组件内的启动、校准、裸机异常处理代码，保留 SWS，关闭 JTAG 引脚复用。
`main.c` 使用硬件 UART0，并显式开启 UART 时钟、关闭保留在配置寄存器中的 CTS/RTS 流控。

生成文件：

```text
build/tl3238_hello/component-firmware/tl3238_hello.bin
build/tl3238_hello/component-firmware/tl3238_hello.elf
build/tl3238_hello/component-firmware/tl3238_hello.map
```

## 烧录及验证（Windows PowerShell）

使用本机 BDT 5.9.5，先释放 COM19：

```powershell
.\project\tl3238_hello\flash.ps1 -BdtDirectory 'D:\MCU_Tools\BDT\release_v5.9.5' -Port COM19
```

脚本依次检查芯片 ID、解锁 Flash、从地址 `0x000000` 烧录、回读逐字节比较、复位，并采集串口输出。
该板使用烧录器的 `usbprint` 驱动和 `config\Cmd_download_tool.exe`；命令在 BDT 根目录运行。
脚本包含本机 BDT 5.9.5 输出文件参数丢失首字符的兼容处理。

仅烧录并回读校验、由其他串口工具观察时，添加 `-SkipSerialVerify`。
只检查已烧录固件的串口输出：

```powershell
.\project\tl3238_hello\capture_uart.ps1 -Port COM19
```

脚本结束后释放串口。

## 本次板上验证

2026-11-13：SWS 读到芯片 ID `0x5687`，Flash MID `0x146085`（1 MB）。
烧录前已备份全部 Flash，原内容全部为 `0xFF`。
最终固件 14,694 字节，回读完全一致；COM19 连续收到 8 行 `hello world`，间隔约 1 秒。

记录位于 `build/tl3238_hello/`：`build.log`、`flash.log`、`uart-COM19.log`、`readback.bin`、`before-flash-20261113.bin`、`verification.json`。
