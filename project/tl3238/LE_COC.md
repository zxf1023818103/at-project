# TL3238 与 Flash Studio iOS 的 LE CoC 吞吐测试

实现依据为 `/home/zxf10/flash-studio-ios/L2CAPBench.swift`，App 提交 `fe1ea9759425af63ad9ad7f285fc5bc3b2185542`，该文件 SHA-256 为 `bf267164e631807046c46aabdc7c8388a8f6cea03d6071e6a10d03d64237f383`。App 源码未修改。

## 共用组件化（2026-11-14）

用户已完成下述原 TL3238 固件的手机测试，具体速率未提供。协议和 NimBLE 服务现集中到 [component/ble_coc_config](../../component/ble_coc_config/README.md)，并由 BL618DG NimBLE/Bluedroid 复用；TL3238 目录只保留 CLI/log 接入。`coc` 命令、UUID、PSM 和原缓冲参数保持一致。

本轮编译产物另存于 `build/tl3238-coc-shared/firmware/`，报告在 `build/ble-coc-shared/`；组件化固件尚未重新烧录/手机测速。下列镜像 SHA、板端内存和硬件检查均为组件化之前的快照。

## 手机操作

1. 板子上电，当前固件广播名为 **TL3238-CoC**，本板地址 `60:3F:FB:70:E5:D3`。
2. Flash Studio 打开 **L2CAP** 页，点击 **连接 / 扫描**。App 自动连接首个兼容设备；测试时只保留目标兼容板上电，其他 BLE 客户端先断开本板。
3. 等待 **LE CoC 已连接 · PSM 129**。选择 10、20 或 60 秒，再点 **双向测速**；先手机发送，再板卡发送，每个方向各持续选定时间，结束时还需排空缓冲并互相确认。
4. 测试期间保持 App 在前台。双方字节数一致、逐字节校验正确且收到完成确认后，App 才显示通过。
5. 点击 **导出 CoC 记录**，保存 `l2cap-throughput.json`。其中包含两个方向的 payload Mbps、接收计数、错误计数及确认在内的耗时。

页面中 `BL618DG · NimBLE · LE CoC` 的 `BL618DG` 是 App 固定文字，不参与设备筛选；TL3238 使用相同 NimBLE 服务。不要以 `--bluedroid-coc` 参数启动这个测试。当前板只支持一个 ACL 连接。

串口 **COM19 / 115200 / 8N1**，TL3238 **PA1 TX → USB RX、PA2 RX → USB TX**。测试结束后执行 `coc` 可查看上传/下载结果；正常应为 `bad=0 errors=0 complete=1`。`interval` 单位 1.25 ms，12 表示 15 ms；`phy=2/2` 表示双向 2M。`stalls` 和 `pauses` 是流控次数，本身不表示丢包。断连或新一轮测试会影响诊断状态，建议先保存日志。

## 发现与流协议

| 项目 | 实现 |
| --- | --- |
| CoC Service | `B1F01000-97B3-4E38-BAF4-91AFB3CD92F0` |
| PSM characteristic | `FFF4`，只读，返回 `81 00`（LE16，PSM 0x81） |
| 通道 | LE Credit Based L2CAP，单通道、双向字节流 |
| 主广播 | CoC UUID，CS 开启时另含 RAS `0x185B` |
| 扫描响应 | SMP UUID `8D53DC1D-1DB7-4CD3-868B-8A527460AA84` + `TL3238-CoC` |
| 帧头 | 16 B：`L2C1` / type:u8 / transport=2:u8 / length:LE16 / A:LE32 / B:LE32 |
| DATA | 最长 4096 B，A 为累计 payload 偏移，B=0，数据为 `(offset+i)%251` |

| type | 作用 | A / B / body |
| --- | --- | --- |
| 1 | START_RX，手机上传 | 秒数 / block 大小 / 空 |
| 2 | START_TX，板卡下传 | 秒数 / block 大小 / 空 |
| 3 | DATA | 偏移 / 0 / payload |
| 4 | FIN | 已发送 payload 总数 / 已用毫秒 / 空 |
| 5 | REPORT | 已接收 payload 总数 / 错误字节数 / LE32 毫秒 + LE32 错误数 |
| 7 | COMPLETE | 板卡发送总数 / 错误数 / 空 |

上传完成顺序为 `FIN → REPORT`，下传完成为 `FIN → REPORT → COMPLETE`。SDU 边界与应用帧无关，解析器支持任意拆分和合并，逐片校验 DATA，不缓存完整 4096 B 帧。未收到终止回执、数量不一致、异常格式或超时都会失败。

## 缓冲与调度

| 参数 | 当前值 |
| --- | --- |
| 本地 RX MTU / MPS | 2048 / 247 B |
| 初始 RX credits | 24 |
| RX SDU 池 | 1 × 2048 B，接收回调处理后立即重新挂入 |
| TX SDU | 最大 245 B，同时受对端 MTU 和 MPS−2 限制 |
| Host 待发队列限制 | 4；msys 空闲不足 6 块时暂停新增发送 |
| msys pool | 16 × 292 B，包含 mbuf 开销 |
| 每次调度 | 最多 4 个 TX SDU，发送期间约 2 ms 再调度 |
| 空闲 | 连接内 100 ms 检查协议超时；无通道时停止定时器 |
| 协议状态 | 132 B，不动态分配应用帧 |

245 B SDU 加 2 B SDU 长度和 4 B L2CAP 头，可放入一个 251 B LL payload。打开通道后请求 251 B DLE、2M PHY、15 ms 间隔；实际参数由双方协商，以日志和 `coc` 为准。较小的发送队列为 TL3238 的 CS/RAS、SMP 和实时任务保留缓冲，因此与 App 原 BL618DG 配套工程的资源配置并不完全一致。

所有通道操作位于 NimBLE Host 队列，credits 耗尽后等待 `TX_UNSTALLED` 再调度；该回调内不递归发送。对 `ble_l2cap_send` 的 mbuf 所有权处理针对固定的 NimBLE 1.10.0 源码实现，升级 Host 时应重新审查。通道清理回调之后 NimBLE 才释放其 RX SDU，故 RX 池保持整个进程生命周期，不在断连时释放。

## 构建与本机验证

```sh
./project/tl3238/build.sh
bash project/tl3238/tests/run_coc_tests.sh

# 移除 CoC，保留原 CS/RAS/mcumgr OTA 配置：
TL3238_ENABLE_COC=OFF TL3238_BUILD_DIR="$PWD/build/tl3238-no-coc" \
    ./project/tl3238/build.sh
```

默认启用 `TL3238_ENABLE_COC=ON`。Windows Python 依赖见 `requirements-ble.txt`：

```powershell
python .\project\tl3238\coc_verify.py --port COM19
```

该脚本检查 UART、真实 BLE 服务发现、FFF4=0x81、SMP 分片、活动镜像 SHA 和连接前后 heap；它不打开 CoC 数据通道，不测 CoC 吞吐量。手机测速时不要同时运行其他 BLE 验证脚本。

2026-11-14 验证记录：

- 默认 CS+RAS+CoC+OTA 固件编译、SWS 烧录及逐字节回读通过。
- CLI/tickless 80 项、CoC 发现/GATT/SMP 19 项、CS/RAS 本地 43 项通过。
- BLE/SMP 回归 90 项通过，包含分片、异常报文恢复、断连重连、远程重启及重启后 echo。
- 原生 ASan/UBSan 协议测试覆盖 13 种拆分/合并大小、双向回执、重试、损坏、越界、超时和计时回绕。
- 真实 NimBLE mbuf/mempool 与生产适配代码的模拟 Host 测试覆盖 200 次 RX SDU 复用、credits 暂停恢复、队列/内存不足、发送失败、100 次重连和 reset 清理。
- 默认、关闭 CoC、关闭 CS、关闭 BLE 四种配置编译通过。
- 初次接入时 **iPhone CoC 数据传输与无线吞吐量由用户另测**；用户随后已告知测完，未导入结果 JSON。上述模拟 Host 测试不代表控制器无线互通结果。

记录保存在 `build/tl3238/`：`coc-summary.json`、`coc-native-tests.log`、`coc-verification.json`、`coc-cs-verification.json`、`coc-ble-verification.json`、`coc-cli-verification.json`、`coc-build-profiles.json`、`coc-readback.bin`。接入前快照在 `before-coc/`。

## 手机测试基线固件及内存（组件化前）

固件 `build/tl3238/firmware/tl3238_freertos.bin`：**428,274 B**，SHA-256：

```text
900bdb7f30b5a0d86f9ee1ae1f1fb4193d8fa48db7cabd0c2a67740a7700c4a5
```

| 项目 | OTA 基线，无 CoC | 当前 CS+CoC+OTA |
| --- | ---: | ---: |
| BIN | 416,978 B | 428,274 B |
| 链接 RAM 分配，含 heap | 160,928 B | 160,320 B |
| FreeRTOS heap 容量 | 38,912 B | 36,864 B |
| 空闲时 heap free | 17,200 B | 12,560 B |
| CS 加密自检后的最低 heap free | — | 10,608 B |
| IRAM 未分配 | 720 B | 720 B |
| DRAM 扣除 2 KiB 启动栈后的未分配空间 | 144 B | 752 B |

接入 CoC 后 heap 容量缩减 2 KiB，为新增静态 Host/msys 结构腾出空间；RX 池和上下文从 heap 常驻分配。空闲 heap 的减少来自新增资源，不是断连泄漏。实际手机测速时的最低余量仍待测量。现镜像距 448 KiB OTA 上限剩余 30,478 B；本轮确认 SMP 镜像管理可用及活动 SHA，完整 A/B/A OTA 实物记录属于接入 CoC 前的基线。
