# USBPcap 抓包

程序：`C:\Program Files\USBPcap\USBPcapCMD.exe`、`C:\Program Files\Wireshark\tshark.exe`。历史根过滤器 `\\.\USBPcap1`，目标地址 18，新会话需要重新验证。

## 启动

无参数启动会进入 `Select filter to monitor` 菜单，不能称抓包已开始。用 `tshark.exe -D` 查 USBPcap 接口，必要时检查 PnP 拓扑；不要无目的地启动多个进程。

```powershell
& 'C:\Program Files\USBPcap\USBPcapCMD.exe' -d '\\.\USBPcap1' -o 'D:\BaiduNetdiskDownload\API\captures\unique-name.pcap' -A --inject-descriptors
```

确认输出文件非空且增长、进程存活后通知用户操作。验证 USB 地址后优先用 `--devices <address>` 代替 `-A` 限定目标；重插时地址可能变化。用新文件名，避免覆盖历史记录。

程序可能拉起管理员子进程；记录本次父/子 PID 和命令行，不按进程名停止其他捕获。

## 停止与分析

优先在原控制台按 `q` 或 Ctrl+C。WSL stdin 可能不连接到提权子进程。必要时用提权脚本附加确切子 PID 的控制台发送 Ctrl+C；不能正常停止才终止本次捕获进程。权限阻止自动停止时请用户在该窗口按 Ctrl+C。

写入中的文件可能因独占句柄而无法读取或复制。先停止再分析，不随意改 ACL。确认捕获进程退出、文件可读。

```powershell
& <tshark> -r <capture> -Y 'usb.idVendor == 0x15a2 && usb.idProduct == 0x300a' -T fields -e frame.number -e usb.bus_id -e usb.device_address
& <tshark> -r <capture> -Y 'usb.device_address == 18 && usb.transfer_type == 3 && usb.data_len > 0' -V
& <tshark> -r <capture> -Y 'usb.device_address == 18' -w <target-only.pcapng>
```

替换实际地址，多总线时同时过滤 `usb.bus_id`。CDC 载荷可能被解析为 `USB Communications and CDC Control` 的 **OUT payload**，只检查 `usb.capdata` 会漏掉指令。先看目标包 `-V` / `-x`，再查询本机 tshark 字段名。

记录字节、方向、端点、帧号、顺序、传输状态。`USBD_STATUS_SUCCESS` 仅证明 USB 传输成功，不是设备端供电确认。抓包没有原电压读回时不要把原值当作包内事实。

## 本次验证依据

- `power-on-blu-only.pcapng`：目标 1.18，OUT 0x03，第 8949 帧 `0C 01`，8950 帧传输成功。
- `voltage-2v2-to-3v3-blu-only.pcapng`：4539 帧 `0D 0C E4`，4540 帧传输成功，无额外开关/查询指令。
- 保持串口且开启后发 `19`，用户反馈未上电；只发 `0C 01` 并保持串口，用户确认上电。查询可能影响输出，固件机制未证实。
- `06` 开始、`07` 停止采集，约 5 秒收到 1,996,800 字节，即 499,200 点。过去的电流数值不能作为未来读数。
