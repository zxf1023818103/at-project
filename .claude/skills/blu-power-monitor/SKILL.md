---
name: blu-power-monitor
description: 使用本地 SDK 控制 BLUEBIRD/BLU 低功耗测试仪的串口连接、输出供电、电压切换和电流采集，或通过 USBPcap 对照上位机通信。适用于 BLU 测试仪、VID 15A2/PID 300A 及本机 API SDK；不用于其他型号的通用电源。
---

# BLU 低功耗测试仪

按用户当前要求执行硬件操作。创建或验证此 skill 本身不授权改变硬件状态。

## 环境与连接

- SDK：`D:\BaiduNetdiskDownload\API`，WSL `/mnt/d/BaiduNetdiskDownload/API`。Python 包为 `python/blu_api_python_100k_202508161306.zip`，入口 `src/blu_all_in_one.py`。
- 实测测试仪 COM17，VID `15A2`、PID `300A`、序列号 `6F677C920017`。COM1 是板载通信端口。每次新会话重新枚举，编号可能变化。
- WSL 通过 Windows `powershell.exe` 和 `.NET SerialPort` 通信：1,000,000 波特、8N1、无握手、DTR/RTS 开启；SDK 打开失败才回退 115200。
- 访问被拒绝时先查看已有控制会话；若被上位机占用，请用户断开串口再重试，不结束用户的上位机进程。

## 协议

| 操作 | 十六进制字节 |
| --- | --- |
| 设置电压 | `0D hi lo`，毫伏值大端，SDK 范围 500–5000 mV |
| 2.2V / 3.3V | `0D 08 98` / `0D 0C E4` |
| 开启 / 关闭输出 | `0C 01` / `0C 00` |
| 开始 / 停止采集 | `06` / `07` |
| 查询信息与校准参数 | `19`，读取到 `END` |

**不要在开启输出后自动追加 `19` 确认。** 此设备的重试中，“开启后查询”未供电，“仅开启、不查询”由用户确认已上电。固件原因尚未证实。先查询并缓存参数，再设置电压，最后开启输出。VDD 回读仅表示电压设置，不能证明输出开启。

切换电压只发电压指令，不附加供电开关、模式切换或采集。读取电流不自动开启或改变供电。关闭输出发 `0C 00` 后释放串口。连续操作沿用串口会话；关闭串口是否影响供电尚未证实，不能当作已确定原因。

## 控制脚本

使用 [scripts/blu-session.ps1](scripts/blu-session.ps1)。WSL 中先用 `wslpath -w` 转换脚本路径，再通过 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File` 调用。所有命令复用同一个 Windows `StateDirectory`。

```powershell
# 在持续执行的终端启动：只打开串口，不写指令。
powershell.exe -NoProfile -ExecutionPolicy Bypass -File <script> -Action Serve -Port COM17 -StateDirectory D:\BaiduNetdiskDownload\API\blu-session
# 从其他终端向同一串口会话发送请求：
powershell.exe -NoProfile -ExecutionPolicy Bypass -File <script> -Action Voltage -Millivolts 2200 -StateDirectory D:\BaiduNetdiskDownload\API\blu-session
powershell.exe -NoProfile -ExecutionPolicy Bypass -File <script> -Action On -StateDirectory D:\BaiduNetdiskDownload\API\blu-session
powershell.exe -NoProfile -ExecutionPolicy Bypass -File <script> -Action Sample -Seconds 5 -StateDirectory D:\BaiduNetdiskDownload\API\blu-session
powershell.exe -NoProfile -ExecutionPolicy Bypass -File <script> -Action Off -StateDirectory D:\BaiduNetdiskDownload\API\blu-session
```

- `Metadata` 保存设备信息，仅在输出未开启或明确诊断查询影响时调用。脚本不会自动查询。
- `Sample` 只发 `06`、`07`，保存原始数据，不改电压或供电。
- `Off` 关闭输出并终止会话；`Stop` 仅关闭串口。
- 请求超时不代表未执行。检查响应、状态、设备现象后再决定重试，不自动重复操作。
- 主机发送成功、USB 传输成功均不等于实际电压或供电确认。物理结果由用户观察或外部测量确认。

## 电流分析

先缓存本次设备的校准信息，不使用其他设备的参数。已供电却无缓存时说明情况，不为读参数悄悄中断供电。默认采集约 5 秒，100 kHz，每点 4 字节小端；忽略不足 4 字节的尾部。

使用 [scripts/analyze-current.py](scripts/analyze-current.py) 调用厂商 SDK 的 `get_samples()`，保留量程切换滤波，不替换为简化公式：

```sh
python3 <skill>/scripts/analyze-current.py <raw.bin> --metadata <metadata.txt> --sdk /mnt/d/BaiduNetdiskDownload/API/python/blu_api_python_100k_202508161306.zip --warmup-seconds 1 --output <result.json>
```

依赖 Python 3 与 pyserial，离线分析不访问设备。报告采样数量、样本推算时长、预热剔除时长及平均/最小/最大电流。只有已知输出电压时才计算相应功耗，并说明是计算值。`Calibrated: 0` 时注明校准状态未确认。统计是 SDK 换算/滤波结果，不宣称精度或完整瞬时峰值。

## 上位机抓包

用户要求抓包时读 [references/usbpcap.md](references/usbpcap.md)。先验证抓包文件在写入，再请用户操作；结束后停止并提取测试仪包。抓上位机时不主动控制设备。

历史证据位置：`D:\BaiduNetdiskDownload\API\captures`。上位机开启输出是 `0C 01`，切换到 3.3V 是 `0D 0C E4`；该目录不保证在其他环境存在。
