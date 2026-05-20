# 概述

本文讲述如何通过两个串口控制 Ameba 系列芯片（D / Dplus / Green2 等）进入烧录模式并烧录固件的方法。

# 接线说明

电脑上接了两个串口 `COM10` 和 `COM25`。`COM10` 负责控制芯片进入 Download Mode，`COM25` 负责和烧录串口 `LOG_TX` 和 `LOG_RX` 进行通信，传输要烧录的固件。

## COM10

| 串口引脚 | 芯片引脚 | 
|---------|----------|
| TXD | Not Connected |
| RXD | Not Connected |
| RTS | CHIP_EN |
| DTR | LOG_RX |

## COM25

| 串口引脚 | 芯片引脚 | 
|---------|----------|
| TXD | LOG_TX |
| RXD | LOG_RX |
| RTS | CHIP_EN |
| DTR | LOG_RX |

## 注意事项

`COM23` 的 `RTS` 和 `DTR` 也和 `COM10` 一样与 `CHIP_EN` 和 `LOG_RX` 连接，但是因为 `COM23` 的 `DTR` 下拉能力不足，导致该串口只能控制芯片复位，而不能控制芯片进入烧录模式，所以才另外接了 `COM10`。在烧录过程包括后续的测试过程中，务必不要下拉该串口的 `RTS` 和 `DTR`，以免芯片起不来。

# 进入烧录模式的方法

基本原理：上电时芯片检测到 `LOG_TX` 拉低，进入烧录模式。

下面的引脚均指 `COM10`：

1. 拉低 `RTS`，使得 `CHIP_EN` 同步拉低，让芯片进入关闭状态；

2. 拉低 `DTR`，使得 `LOG_TX` 同步拉低；

3. 拉高 `RTS`，使得 `CHIP_EN` 同步拉高，芯片上电，此时进入烧录模式；

4. 拉高 `DTR`，避免后续 `LOG_TX` 通信异常；

5. 此时应该能在 `COM23` 上以 `115200bps` 检测到芯片在不停发出 `0x15`，代表芯片成功进入烧录模式；

6. 如果没有检测到 `0x15`，需要重复上面的步骤。

# 命令行烧录方法

阅读 `platform/ameba-rtos/tools/ameba/MP_ImageTool_1_N/MigrationGuide.txt` 使用 `platform/ameba-rtos/tools/ameba/MP_ImageTool_1_N/Ameba_1-N_MP_ImageTool.exe` 工具进行烧录。AmebaD 对应的芯片型号选项为 `AmebaD`。

## 注意

为了确认合成固件无误，请烧录 `Image_All.bin` 文件，不要烧录分开的 `km0_km4_image2.bin` 等文件。
