# AmebaD (RTL8721D) — TCP 跑流时 KM0 / KM4 可用堆内存变化

调查:双核 driver-only image 在 TCP 满速跑流时,两个核各自的 FreeRTOS 可用堆
(`xPortGetFreeHeapSize`)怎么变。结论先行:**TCP 数据通路的内存压力全在 KM4
(协议栈 + host 驱动侧),KM0(跑 WiFi 固件)的 FreeRTOS 堆纹丝不动。**

## 方法

- 临时给两个核各挂一个 ~1 Hz 的堆上报任务(**ad-hoc 仪表,跑完已 revert,不入树**),
  都 `DiagPrintf` 到**共享的 LOGUART**(KM0 在 boot 期 `LOGUART_SetBaud(1500000)` 后
  把 shell RX 交给 KM4,但 TX/DiagPrintf 仍走同一个物理 UART),所以一次 `sniff_log`
  抓取里 `[KM4-HEAP]` 和 `[KM0-HEAP]` 两条线同时出现,天然时间对齐。任务体:
  ```c
  static void heapmon(void *p){ (void)p; for(;;){
      DiagPrintf("[KM4-HEAP] free=%d min=%d\n\r",
          (int)xPortGetFreeHeapSize(), (int)xPortGetMinimumEverFreeHeapSize());
      vTaskDelay(pdMS_TO_TICKS(1000)); } }
  ```
  - KM4:加进 `app_km4/main.c`,`vTaskStartScheduler` 前 `xTaskCreate`。
  - KM0:同样的任务加进 `app_km0/main.c`(标签 `[KM0-HEAP]`),`vTaskStartScheduler` 前
    `xTaskCreate`(KM0 main.c 需补 `#include "FreeRTOS.h" "task.h"`)。
  - 每条打印 `free` 与 `min`(min-ever,heap_4/5 跟踪的历史最低)。
- 流量:设备端 iperf2 TCP client(`app_km4/iperf_client.c`,TX 15 s,8 KB chunk)
  打到 WSL host 的 `iperf -s -p 5001`(WSL 镜像网络 = 192.168.2.73,设备 DHCP 拿
  192.168.2.145,同 LAN 经 AP 直达)。
- 抓取:`sniff_log.ps1 -Port COM29 -Baud 1500000 -Seconds 45`,覆盖 boot →
  (KAT/wolfCrypt bench)→ wifi_on → connect → DHCP → 15 s TCP TX → 收尾。

实测吞吐两侧一致:设备自报 `TX 20561920 bytes / 15004 ms = 10960 Kbit/s`,host
`iperf -s` 收 `19.6 MBytes / 15.01 s = 11.0 Mbits/sec`——即本栈 ~11 Mbps 软件天花板
(见 [[amebad_throughput_results]])。

## 结果

### KM4(HP,跑 FreeRTOS-Plus-TCP + host WiFi 驱动 lib_wlan.a)—— 随流量起伏

| 阶段 | free (B) | min-ever (B) |
|---|---|---|
| WiFi init 完 | 329040 | 329040 |
| 驱动 bring-up(wifi_on) | ~280000 | ~280000 |
| 关联 + DHCP(idle-connected) | 294200 | 275608 |
| **TCP streaming(15 s)** | **250456**(全程恒定) | **248824** |
| TCP 关闭后 | 290232 | 248824 |

→ **起 TCP 吃掉约 44 KB** KM4 堆(294K → 250K):FreeRTOS+TCP 的网络 buffer + socket
+ 8 KB iperf TX buffer。整个 15 s **稳在 250456**(不持续增长,无泄漏迹象),socket
关闭后回到 ~290K。全程历史最低 **248824**(~248 KB),离 0 还很远,余量充足。

### KM0(LP,跑 WiFi 固件 lib_wifi_fw.a)—— 平的

```
[KM0-HEAP] free=9520 min=9520    ← idle / connect / TCP / 收尾 全程不变
```

→ KM0 的 FreeRTOS 可用堆**恒为 9520 字节**,min == free,流量前后毫无变化。

## 分析

- **TCP 内存压力只在 KM4**:协议栈(FreeRTOS+TCP 的 network buffer 池 + socket)和
  host 驱动都在 KM4;一条 TCP 连接的瞬时占用 ~44 KB,跑流期间恒定、关闭即释放,
  无累积——说明没有连接级泄漏。
- **KM0 堆为何不动**:KM0 的 WiFi 固件(lib_wifi_fw.a)做的是逐帧收发,但它用的是
  **静态 / DMA 描述符 buffer**,不走 FreeRTOS `pvPortMalloc`,所以 `xPortGetFreeHeapSize`
  看到的 KM0 堆与吞吐无关。9520 B 是 KM0 init 后的稳态余量(LP 核 RAM 本就小)。
- 实务含义:**调 TCP 吞吐/并发时盯 KM4 堆**(每条连接 ~几十 KB);KM0 那 9.5 KB
  余量不受流量影响,但本就紧——KM0 上别再加吃 FreeRTOS 堆的东西。

## 复现

1. 把上面的 `heapmon` 任务临时加进 `app_km0/main.c` + `app_km4/main.c`(~1 Hz,
   `vTaskStartScheduler` 前 xTaskCreate)——本次已 revert,重测时再加。
2. host:`iperf -s -p 5001 -i 1`(WSL,= 192.168.2.73)。
3. `python3 component/amebad/image_tool/build_image_all.py` → flash → 设备 boot 后
   自动连 AIoT + 跑 iperf TCP client。
4. `sniff_log.ps1 -Port COM29 -Baud 1500000 -Seconds 45`,grep `HEAP|iperf`。

## 关键文件
- 堆上报:临时仪表(已 revert),挂点 `app_km4/main.c` / `app_km0/main.c` 的
  `main()`(见上方 §方法 的任务体)。
- 流量:`project/amebad/app_km4/iperf_client.c`(device→host TCP TX,常驻)。
- 吞吐天花板背景:[[amebad_throughput_results]]
