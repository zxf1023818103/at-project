---
name: reference-amebad-imagetool
description: "AmebaD (RTL8721D) chip-name for MP_ImageTool_1_N/Ameba_1-N_MP_ImageTool.exe is \"AmebaD\"; flash flow via two COM ports"
metadata: 
  node_type: memory
  type: reference
  originSessionId: 9b675393-9650-43a8-8c91-1fc0aa4da2b2
---

# AmebaD 烧录 via `MP_ImageTool_1_N/Ameba_1-N_MP_ImageTool.exe`

工具路径：`platform/ameba-rtos/tools/ameba/MP_ImageTool_1_N/Ameba_1-N_MP_ImageTool.exe`
脚本：`project/amebad/flash.ps1`

## 关键坑：芯片名

新版工具改用 device profile（`Devices/Profiles/*.rdev` + `Devices/Floaders/floader_<dev>.bin`），CLI 参数 `-c` 接 device 名而不是 RTL chip 名：

| Platform | `-c` value |
|----------|-----------|
| AmebaD | `AmebaD` |
| AmebaDplus | `AmebaDplus` |
| AmebaGreen2 | `AmebaGreen2` |
| AmebaLite | `AmebaLite` |
| AmebaSmart | `AmebaSmart` |
| AmebaZ | `AmebaZ` |

历史：旧版工具 `Ameba_1_N_MP_ImageTool_v2.5.21/`（已删）用 RTL 型号名 `rtl872xd`、`rtl8721dx` 等，那时 AmebaD 的正确名是 `rtl872xd`，传 `rtl8721dx` 会拿错 floader 导致 `download flashloader error`。

**How to apply:** 烧 AmebaD 出的 `Image_All.bin` 一律 `-c AmebaD`。完整列表在 `MP_ImageTool_1_N/Devices/Profiles/*.rdev` 文件名。

## 烧录硬件接线 / 时序

见 [doc/ameba_image_download.md](../../../home/zxf/at-project/doc/ameba_image_download.md)。两路串口：

- `COM10` — RTS→CHIP_EN, DTR→LOG_RX（仅做 download-mode 进入时序，不传数据）
- `COM25` — TXD/RXD→LOG_TX/LOG_RX（烧录数据）。**它的 RTS 也接 CHIP_EN，DTR 接 LOG_RX**，开关 port 时会扰动芯片，所以 ImageTool 用 `-g false`（DtrRtsConfig=false），手动用 COM10 拉一次 download mode 即可。

进入 download mode 步骤（在 COM10 上）：
1. RTS 拉低 → CHIP_EN 低
2. DTR 拉低 → LOG_RX/LOG_TX 低
3. RTS 拉高 → 芯片上电，采到 bootstrap 低 → 进 download mode
4. DTR 拉高，避免后续通信被压住

确认成功的标志：COM25 @ 115200 上不停收到 `0x15`。

## CLI 调用骨架

```powershell
.\Ameba_1-N_MP_ImageTool.exe `
  -c AmebaD `
  -i .\Image_All.bin `
  -b 1500000 `
  -g false `
  -v true `
  -s true `       # savelog → 在 <ToolDir>\log\YYYY_MM_DD_HHMMSS.txt
  -d COM25
```

**注意：** 工具退出码恒为 0，**不要靠 $LASTEXITCODE 判断成功失败**，必须 grep stdout 的 `FAIL` / `SUCCESS`。`-y` (downloaddisplay) 在新版里已被移除。

## 烧 Image_All.bin（不是分体 bin）

必须烧 `project/amebad/out/Image_All.bin`（也即 [[reference-amebad-flash-layout]] 里的合成镜像），不要分别烧 `km0_boot_all.bin` / `km0_km4_image2.bin`——doc 明确要求，便于校验合成无误。

参考 [[reference-wsl-powershell-serial]] 跨 WSL/PowerShell 驱动串口的细节。
