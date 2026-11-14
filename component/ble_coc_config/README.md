# 共用 LE CoC 测速组件

提供 Flash Studio iOS `L2CAPBench.swift` 使用的双向 LE Credit Based L2CAP 测速。TL3238、Bouffalo NimBLE 和 BL618DG Bluedroid 共用同一份 L2C1 编解码、数据校验与完成回执；组件中没有芯片驱动、引脚或 Controller 初始化。

## 组成

| CMake target | 内容 | 依赖 |
| --- | --- | --- |
| `ble_coc_protocol` | `coc_stream.c`，流式拆包/粘包、逐字节校验、计数、超时、双向回执 | C99 标准库 |
| `ble_coc_nimble` | NimBLE GATT PSM 服务、CoC 通道、mbuf、credits、Host 队列限流、NPL 调度 | NimBLE Host + 对应 NPL/分配器 |
| `ble_coc_bluedroid` | Bluedroid GATT PSM 服务、CoC 通道、ACL 队列限流、独立广播与 BTU 调度 | 本仓库 Bluedroid + FreeRTOS |

三个 target 都是携带源码的 `INTERFACE` 目标。后端应 **PRIVATE 链接到已有的 Host library**，源码在该 library 内编译，使用该芯片的 syscfg、NPL、allocator 和编译选项。无需再手工添加 `coc_stream.c`，也不要把后端 PUBLIC 链接出去导致应用重复编译。单独链接协议目标不需要蓝牙或 RTOS 头文件。

公共头文件位于 `include/`；协议测试及后端模拟测试位于 `tests/`。原 TL3238 的 `tests/run_coc_tests.sh` 保留为转发入口。

## NimBLE 接入

已有 Host target、NPL、Controller/HCI 传输和 NimBLE 头文件后：

```cmake
if(NOT TARGET ble_coc_nimble)
    add_subdirectory("${AT_ROOT}/component/ble_coc_config" ble_coc_config)
endif()
target_link_libraries(my_nimble_host PRIVATE ble_coc_nimble)
target_include_directories(my_nimble_host PRIVATE "${NIMBLE_ROOT}/nimble/host/src")
# 若 init/GAP 回调位于另一个 application target，向它提供公共头文件路径。
target_include_directories(my_app PRIVATE "${AT_ROOT}/component/ble_coc_config/include")
```

在 Host 的 syscfg 中启用 `BLE_L2CAP_COC_MAX_NUM=1`、`BLE_L2CAP_COC_MPS=247`、`BLE_L2CAP_COC_SDU_BUFF_COUNT=1`，并提供足够的 msys 缓冲；当前小配置使用 16 块。所有 Host 源码必须看到一致的 syscfg。

```c
#include "ble_coc_nimble.h"

/* nimble_port_init() 之后、启动 Host event queue 之前；失败则终止本次启动。 */
int rc = ble_coc_nimble_init(my_log); /* void my_log(const char *, ...)，可传 NULL */

/* 从已有的 GAP callback 转发全部事件。 */
ble_coc_nimble_gap_event(event);

/* 从已有的 Host reset callback 转发。 */
ble_coc_nimble_reset();

/* 放到已有广播中，服务 UUID 来自公共常量。 */
fields.uuids128 = (ble_uuid128_t *)&ble_coc_nimble_uuid;
fields.num_uuids128 = 1;

/* 可选 CLI/状态界面；组件本身不依赖 FreeRTOS-CLI。 */
ble_coc_nimble_status(output, sizeof output);
```

初始化仅调用一次；组件和 RX pool 常驻至 Host 进程结束。一次只接受一个测速通道。通道操作、GAP/reset 转发在 Host 上下文执行；状态快照使用 NPL 临界区，要求单核 Host，或 NPL 能跨核串行化访问。通道打开后请求 DLE 251、2M PHY、15 ms 间隔；不支持某参数的 Controller 可以拒绝请求，测速继续使用协商结果。

NimBLE 后端使用公开 CoC API，并通过 `ble_hs_priv.h` 设置初始 credits、读取待发送队列。已针对仓库 NimBLE **1.9.0 / 1.10.0** 检查 mbuf 所有权并运行相同测试。升级 Host 时需要重新核对私有结构、`send`/`recv_ready` 错误分支，以及断连回调之后才释放 RX SDU 的顺序。

## Bluedroid 接入

把 `ble_coc_bluedroid` PRIVATE 链接到已有 Bluedroid Host target，并提供其公共/私有头文件及 FreeRTOS；启用 Host 自身的 LE CoC 编译配置。本仓库的 `component/bluedroid_config/CMakeLists.txt` 是接入示例。

平台完成 Controller、HCI、`esp_bluedroid_init()` / `esp_bluedroid_enable()` 后，包含 `ble_coc_bluedroid.h` 并调用一次 `ble_coc_bluedroid_init()`。该后端是独立测速服务，拥有 GAP callback、广播及 BTU 投递任务，适用于现有独立测速模式。与已有产品 GAP 逻辑合用时需由平台协调回调及广播。

L2CAP 操作只在 BTU 执行，拥塞回调不递归发送。后端读取本仓库 Bluedroid 的 `l2c_int.h` ACL 队列；升级该 Host 时也要核对私有结构和 TX/RX 缓冲所有权。

## 配置

通过消费后端的 Host target 的 `target_compile_definitions(... PRIVATE ...)` 覆盖 `ble_coc_config.h` 默认值。RX MPS 由 Host 自己的配置决定，组件不重复定义。

| 配置宏 | 默认 / TL3238 | BL618DG NimBLE | BL618DG Bluedroid |
| --- | ---: | ---: | ---: |
| `BLE_COC_PSM` | 0x81 | 0x81 | 0x81 |
| `BLE_COC_RX_MTU` | 2048 | 2048 | 2048 |
| `BLE_COC_RX_CREDITS` | 24 | 24 | 24 |
| `BLE_COC_RX_BUFFERS`，NimBLE 专用 | 1 | 8 | Host 管理 |
| `BLE_COC_TX_SDU_MAX` | 245 | 1024 | 1024 |
| `BLE_COC_TX_QUEUE_LIMIT` | 4 | 8 | 8 |
| `BLE_COC_MSYS_RESERVE`，NimBLE 专用 | 6 | 12 | — |
| `BLE_COC_HEAP_RESERVE`，Bluedroid 专用 | 32768 | — | 32768 |
| `BLE_COC_TX_BURST` / `BLE_COC_TX_INTERVAL_MS` | 4 / 2 | 4 / 2 | 4 / 2 |
| `BLE_COC_IDLE_INTERVAL_MS`，NimBLE 专用 | 100 | 100 | — |
| `BLE_COC_CONN_INTERVAL`，1.25 ms 单位 | 12 | 12 | 12 |

TX 实际长度还受对端 MTU 和 MPS−2 限制。NimBLE 从该 Host 的 allocator 分配 RX pool 和上下文，协议状态只有 132 B，不缓存完整 4096 B 应用帧；TX 临时栈缓冲为 `BLE_COC_TX_SDU_MAX`，增大该值时需相应检查 Host 栈余量。

Bluedroid 的广播名由 `BLE_COC_DEVICE_NAME` 指定，默认 `LE-CoC-BD`；BL618DG 使用 `BL618DG-CoC-BD`。NimBLE 广播和设备名由应用管理，TL3238 为 `TL3238-CoC`，BL618DG 为 `BL618DG-CoC`。

## iOS 协议

| 项目 | 值 |
| --- | --- |
| NimBLE service | `B1F01000-97B3-4E38-BAF4-91AFB3CD92F0` |
| Bluedroid service | `B1F01001-97B3-4E38-BAF4-91AFB3CD92F0` |
| PSM 特征 | FFF4，只读 LE16，默认 `81 00` |
| 帧头 | `L2C1` + type:u8 + transport=2:u8 + length:LE16 + A:LE32 + B:LE32 |
| DATA | 最大 4096 B，A 为偏移，B=0，payload[i]=(offset+i)%251 |
| 上传 | START_RX(1) → DATA(3) → FIN(4) → REPORT(5) |
| 下传 | START_TX(2) → DATA(3) → FIN(4) → REPORT(5) → COMPLETE(7) |

START 的 A 为秒数，B 为 block 大小；FIN 的 A 为 payload 总字节数，B 为毫秒；REPORT 的 A 为接收字节数、B 为坏字节数，body 为 LE32 毫秒 + LE32 错误数；COMPLETE 的 A 为发送字节数、B 为错误数。缺回执、计数不符、坏字节、格式错误或超时不算通过。LE SDU 与应用帧边界独立。

手机使用 Flash Studio 的 **L2CAP → 连接 / 扫描 → 双向测速**，结果导出 `l2cap-throughput.json`。其他 Host 可直接复用 `ble_coc_protocol`，实现 feed / prepare / peek / sent 适配；`sent()` 只能提交传输层已接受的字节，遇到 busy 必须保留原数据等待重试。

## 验证

```sh
bash component/ble_coc_config/tests/run_tests.sh
```

运行协议 ASan/UBSan 测试，并分别用本地 NimBLE 1.9 和已缓存的 1.10 真实 mbuf/mempool 代码测试小/大两组配置。可用 `BLE_COC_NIMBLE_ROOT` 选择一个 Host 源目录，用 `BLE_COC_TEST_BUILD_DIR` 指定输出目录。覆盖拆包/粘包、双向确认、错误数据、计时回绕、credits 暂停恢复、队列/内存限流、发送失败和重复断连。

2026-11-14 初次组件化编译：TL3238 CS+RAS+OTA+CoC、BL618DG NimBLE CoC、BL618DG Bluedroid CoC 均通过；新组件在三种构建中均通过 `-Wall -Wextra -Werror` 检查。该阶段产物与报告在 `build/ble-coc-shared/`，TL3238 产物在 `build/tl3238-coc-shared/firmware/`。

同日共用持久化完成后，保留原 iPhone 13 Pro 绑定，在 TL3238 最终固件上实测
每个方向各 60 秒，双方计数一致、坏字节和错误计数为 0：

| Host | 手机 → 板端 | 板端 → 手机 |
|---|---|---|
| NimBLE（完整 CS/RAS/OTA/CoC） | 0.248 Mbps / 1,872,896 B | 0.197 Mbps / 1,484,800 B |
| Bluedroid | 0.455 Mbps / 3,423,232 B | 0.265 Mbps / 1,987,584 B |

均为 LE 2M；NimBLE 为 CPU 96 MHz / 连接间隔 30 ms，Bluedroid 为 48 MHz /
15 ms，不能直接据此比较 Host 性能。板卡无天线，早期测试曾出现超时，失败报告
也保留。两次最终测试均显式指定 PSM 129，绕过切换 Host 后 iOS 的残留 GATT
缓存，因此不代表服务发现通过。详细原始结果及固件 SHA 见
[验证报告](../../build/ble-store-validation/unified-validation-summary.json)。


## iPhone 自动化

`tools/prepare_ios_bench.py` 从本地 Flash Studio 的传输实现生成独立 CoC Bench
工程，使用单独 bundle `cn.zenghome.flash-studio.cocbench`。只增加按 Host 选择
服务的启动参数，避免同一蓝牙身份切换 Host 时选中 CoreBluetooth 缓存的另一套
CoC 服务；传输协议保持原实现。原文件和生成文件 SHA 保存在 `source.json`。

```sh
python3 component/ble_coc_config/tools/prepare_ios_bench.py \
  --flash-studio /path/to/flash-studio-ios --output /tmp/coc-bench
# 在已授权 Mac 上编译、签名并安装该独立 App 后：
python3 component/ble_coc_config/tools/iphone_bench.py \
  --mac user@mac --device IPHONE_DEVICE_ID \
  --bundle cn.zenghome.flash-studio.cocbench --stack NimBLE --seconds 60 \
  --output /tmp/nimble-throughput.json
```

SSH 优先使用密钥，也支持 `SSHPASS` 环境变量；报告不保存凭据。每轮拒绝旧时间戳
报告，分别核对发送/接收字节、坏字节及错误计数。运行完 App 仍可能保持连接，
切换固件或其他客户端前应终止该测试进程。`--stack Bluedroid` 选择另一套服务。

若同一绑定身份切换 Host 后，iOS 仍缓存另一套 GATT 表，可在自动化命令中加
`--psm 129`（组件默认 PSM）。这会直接打开已知通道；报告的
`runner.psm_source` 为 `explicit`，不能算作 GATT 服务发现通过。正常发现流程
不指定此参数，记录为 `gatt`。不需要删除手机绑定来运行这项数据面测试。
