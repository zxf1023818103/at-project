---
name: amebad-freertos-tcp-bringup
description: AmebaD KM4 跑 FreeRTOS-Plus-TCP(替代 lwip 拿 DHCP IP)的接法 + 4 个实机踩坑;wl80211/跨平台 FreeRTOS+TCP 复用同套路
metadata:
  type: reference
  originSessionId: 2c7314f2-7f7e-46e5-84f0-75d717be0da0
---

AmebaD KM4 上用 **FreeRTOS-Plus-TCP V4.0.0**(`component/thirdparty/freertos-plus-tcp`,
配 freertos-kernel V10.6.2)替代 lwip 做 IP 层,实机拿到 DHCP IP 跑通(commit 981dba0)。
组件 `component/amebad/freertos_tcp/`。验收:连 AIoT(WPA2-PSK)→ DHCP → 192.168.2.145。

**架构(Realtek 驱动 ↔ FreeRTOS+TCP 桥)**:
- 驱动收发 seam 全在我们 vendored 的 `wlan/src/lwip_intf.c`:驱动调 `netif_rx(idx,len)`
  →它调 `ethernetif_recv(&xnetif[idx],len)`;`rltk_wlan_send/recv` 是 copy-based
  scatter-gather(不要 headroom)。**netif_rx 是 task 上下文**(netif_rx_sema),非 ISR。
- **RX 桥**:override `ethernetif_recv`(别编 lwip 的 ethernetif.c)→
  `pxGetNetworkBufferWithDescriptor` + `rltk_wlan_recv` copy + **设
  pxBuf->pxInterface + pxBuf->pxEndPoint(FreeRTOS_MatchingEndpoint)** +
  `xSendEventStructToIPTask(eNetworkRxEvent)`。
- **TX**:V4.0.0 多 endpoint `pfOutput(pxInterface,pxBuf,release)` → `rltk_wlan_send`。
- **netif 注册**:不用 lwip 的 netif_add;直接 `rltk_wlan_set_netif_info(0,&xnetif[0],mac)`。
  `xnetif[]` 是驱动唯一的 lwip 类型依赖(lwip_intf.c 访问其字段),单独在
  realtek_xnetif.c 用 lwip/netif.h 定义。
- **init 顺序**:FreeRTOS_FillEndPoint(bWantDHCP=pdTRUE)+ **FreeRTOS_IPInit_Multi 必须在
  wifi_connect 之前**(关联时驱动就开始 RX,buffer 池/IP task 得先在)。事件 hook
  `vApplicationIPNetworkEventHook`(ipconfigIPv4_BACKWARD_COMPATIBLE=1 用单参版)里
  FreeRTOS_GetAddressConfiguration 读 IP。

**CMake 解耦**:lwip IP 不链;`wifi`/`wlan_glue` 改 link 新建的 `amebad_lwip_headers`
(纯头 INTERFACE,**别 link 库否则 lwipcore 进 app 的 cross_refs LINK_GROUP 成环**);
`amebad_wifi` 去掉 lwip_netconf.c;app_km4 link group 用 amebad_freertos_tcp 换
amebad_lwip/lwipcore;`freertos_tcp_compat.c` 弱桩 wifi_conf 残留 lwip 引用
(LwIP_Init/dhcp_*/promisc/netif_set_* 等,非走的路径)。

**4 个实机踩坑(都崩 Hard Fault,non-secure handler 不打寄存器,要靠隔离法定位)**:
1. **wifi_get_mac_address 返回字符串 "xx:xx:xx:xx:xx:xx"(18B),不是 6 字节** —— 给
   `unsigned char mac[6]` 直接栈溢出。要 char[20] 接 + 解析成 6 字节。
2. **LwIP_GetMAC 不能桩成返回 NULL** —— connect 期的 mgmt-frame handler
   (wifi_conf.c:429)`memcmp(DA, LwIP_GetMAC(&xnetif[0]), 6)`,NULL→崩。要真返回
   `pnetif->hwaddr`(故 amebad_register_wlan_netif 必须先设 xnetif.hwaddr)。
3. **RX descriptor 必须设 pxInterface + pxEndPoint** —— 否则 IP task 路由时崩(V4.0.0
   多 endpoint)。
4. **RX 桥要等 FreeRTOS_IPInit 之后**才能 pxGetNetworkBuffer —— 加 xRealtekTcpReady
   flag,未 ready 就 drain+drop(还得 drain,否则驱动 RX ring 卡)。

隔离法:逐段 `#if 0`(连接前 setup / RX push / 整个 FreeRTOS+TCP)+ 二分,确定崩在
wifi_connect 本身 vs RX vs TX vs IP task。connect 成功的标志日志:`association
success` → `set pairwise/group key alg:4(AES)`。

**TCP 吞吐调优(iperf,d47c374)**:1.5 → ~11 Mbps 峰值(8x)。杠杆从大到小:
1. **关 power-save = 决定性**:`wifi_set_power_mode(0,0)` 同时关 IPS+LPS。单
   `wifi_disable_powersave()` 只关 LPS,残留 IPS 让 RTT ~17ms;关全 RTT→4ms,
   throughput 翻几倍。这是 #1 必做项。
2. 驱动 skb 池 `MAX_SKB_BUF_NUM`(rtw_opt_skbuf.c)8→16 加深 TX/RX 流水(16>8;
   32 反而差,信道抖动)。
3. KM4 `SystemSetCpuClk(CLK_KM4_200M)` 跑满频(=最高档,无更高)。
4. iperf 任务优先级 +2(别升高——升到 +5 会饿死驱动 xmit 线程,掉到 6.7)。
坑:DiagPrintf 不支持 %u/宽度符;64 位除法没链(__aeabi_uldivmod),吞吐用
32 位 (bytes/ms)*8。iperf_client.c=TX(连 WSL iperf -s),iperf_server.c=RX sink
(host iperf -c),iperf_udp_client.c=UDP blast 判别器(host iperf -u -s)。

**TCP ~11 Mbps 平台期的真因定论(79799ec;先推翻"airtime 天花板"、再推翻"tick
量化 ACK-clock",最终=IP-task 每包处理上限)**:旧误判"2.4GHz 拥挤/1x1 HT20 仅
2.4G"——全错:RTL8721D 双频(扫到 ch36/40/149),连的是 5G AIoT BSSID(..84)。
三个实机 pps 把瓶颈钉死(判别器:iperf_udp_client.c=UDP TX blast、
iperf_udp_server.c=UDP RX sink):
- **UDP TX = 22.7 Mbps / 1930 pps**(打满驱动 skb 池 skb_unavailable=673)→ radio
  能过 17,瓶颈在栈不在空口(RSSI -42、5G 干净)。
- **UDP RX = ~14.5 Mbps / ~1300 pps**(host `iperf -u -c <dev> -b40M`)。
- **TCP TX = ~11 Mbps / ~930 pps**。
**不是 window**(lTxWinSize 12→32 吞吐 10.9→9.7 反降、RTT 13→38ms 纯 bufferbloat)、
**不是主频**(CLK_KM4_200M 即最高)、**不是 tick**(RX 1300>1000pps 已越过 1kHz
tick,故非 tick 量化;旧"~1包/tick"是 930≈1000 巧合)、**不是 copy**(~7µs≈1%)。
**真因=单 IP task 每包处理上限**:TCP TX 每段串行付 1 次 send + (延迟ACK)~半次
ACK-RX,1/(0.52+~0.5·0.77)ms ≈ ~1100 pps 吻合 ~930。每包 ~0.5-0.8ms
(~100-150k cycles)落在闭源 lib_wlan.a 的 rltk_wlan_send/recv 路径 + IP-task hop。
→ **~11M 是本栈+驱动的 TCP 实际天花板,带宽走 UDP(22.7M)**。
**BufferAllocation_1 已否决**:静态 buffer 池 61KB BSS 溢进闭源驱动硬编码占用的
SRAM(ld 看不见驱动 RAM→不报 overflow),wifi_connect 即挂;且 malloc 是 µs 级、
治不了 100k-cycle/包的驱动开销。留 BufferAllocation_2(heap)。
要再啃只剩闭源驱动每包成本(rltk_wlan_send/recv ~0.5-0.8ms 花在哪)——深且不确定。
**实机驱动技巧(sandbox 限制)**:本机 sandbox 会秒杀任何对"在收的设备"发的
host→device UDP blast(任意速率/前后台,零投递;只有设备不在收时存活)→ RX 数得在
原生 WSL 终端手跑 blast、我侧 sniff_log.ps1 抓设备自报 pps。烧录单口 COM29
(flash.ps1 -BootPort COM29 -FlashPort COM29 -SkipModeCheck;sniff_log.ps1 -NoReset
不复位只读)。

后续:① 这套 NetworkInterface 直接可复用到 wl80211(博流)—— seam 一样(注册 input cb +
mac_tx/rx);② sockets/TCP、AP 模式、把 lwip 彻底退役都是后话。相关:[[amebad-wifi-bringup]]
(scan 里程碑,lwip 版)、[[amebad-asdk-toolchain-path]]。
