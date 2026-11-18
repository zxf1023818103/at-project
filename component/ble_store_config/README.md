# 共用蓝牙持久化

NimBLE 和 Bluedroid 使用同一份 LittleFS `/ble.bonds`（BLS3 v1）。本地 IRK、
双方 LTK/IRK/CSRK、安全属性、签名计数及 Bluedroid 本地根密钥都用显式字节编码，
不保存 Host 的 C 结构体。平台只提供已有的 `vfs_lfs_mount()`、Flash 驱动和锁；
组件不修改分区，也不创建第二个文件系统挂载。

| 层 | 文件 | 职责 |
|---|---|---|
| 共用存储 | `ble_bond_store.c` | 格式校验、容量检查、BLS2 导入、原子提交、错误锁存 |
| 文件事务 | `lfs_blob.c` | 精确长度读取，临时文件写入、sync/close、rename |
| NimBLE 适配 | `../nimble_config/store/ble_store_littlefs.c` | Host store 回调和本地 IRK 回调 |
| Bluedroid 适配 | `../bluedroid_config/bond_config.c` | 共用记录与内存 config 的双向转换 |
| 非密钥设置 | `../bluedroid_config/nvs_lfs.c` | LittleFS NVS namespace；提交前剥离 LE 密钥 |

默认容量为 4 条本地安全记录、4 条对端安全记录和 16 条订阅，文件 992 字节。
产品可通过 CMake `BLE_BOND_MAX_BONDS` / `BLE_BOND_MAX_CCCDS` 调整。两个 Host
须使用相同配置；读取其他容量文件时按实际记录数检查，空间不足会报错，不丢绑定。
Bluedroid 的绑定上限同步使用该配置。

## 迁移与切换

- 新文件不存在时，任一 Host 可导入旧 `/nimble.bonds`（BLS2），完整检查长度、
  CRC、记录和容量；保留原文件。NimBLE 还支持旧 `/ble_store.bin` 的严格 ABI 导入。
- Bluedroid 首次运行时合并旧 `bt_config.conf` NVS 中不同对端的绑定；地址冲突时
  共用文件中的记录优先，本地 IRK 也优先保留。迁移标记与密钥一起原子提交。
  以后不会再次从旧文件恢复已删除的绑定。
- Bluedroid 的 config 只在 RAM 中包含 Host 格式的 LE key。写入原 NVS 前移除这些
  key 和 LE 地址/认证字段，非安全的名称、Classic/Profile 设置仍由 NVS 保存。
  所有 LE 安全数据只从 `/ble.bonds` 恢复。
- 切换 Host 必须保持同一设备的 public/static identity address，不能另造地址。
  TL3238 使用原 factory MAC；共用 NimBLE 示例和 TL3238 Bluedroid 都已撤掉
  原先为隔离测试而派生另一套随机地址的逻辑。NimBLE 的 sync 回调统一调用
  `ble_store_littlefs_on_sync()`，通过标准 Service Changed 通知已订阅的绑定
  客户端重新发现 GATT 表。新增平台同样调用这个共用接口。旧 BLS2 未记录
  本地地址；曾使用自定义/派生随机地址的产品，迁移时仍需保持原地址。
- SC 两个方向使用同一个 LTK；适配器补齐 Host 需要的本地/对端视图。地址在文件中
  使用低字节在前顺序，Bluedroid 的 `BD_ADDR` 在适配边界转换。
- Bluedroid 新的传统配对在密钥分发时额外保存 EDIV/Rand，支持之后交给 NimBLE。
  旧 Bluedroid 的传统 LENC 文件只有 DIV，没有 EDIV/Rand，无法反推；这种记录
  在 NimBLE 中返回不支持，不能冒充完整迁移。此次 iPhone 原绑定使用 SC，不受影响。
- 当前格式是一套本地身份；Bluedroid 的双本地身份 `PseudoBond` 不会混入，导入会
  明确失败。CCCD 带 GATT 数据库标识（默认 1 为 NimBLE），不会把 NimBLE 的句柄
  应用到 Bluedroid 的另一份属性表。应用改变 GATT 布局时应更新数据库标识。
  两套 Host 的 GATT 表并不相同，手机残留缓存属于另一个互通问题，不能仅凭
  加密重连成功就认为服务发现也通过。测速工具可用显式 PSM 独立验证数据面。

## 提交与错误处理

同一时刻只有一个 Host 拥有存储。NimBLE 在 Host 锁内操作，Bluedroid 在 BTC
config 锁内操作。LittleFS 的锁只保护单次操作，不能替代整个文件事务的串行化。

一次成功配对的 Bluedroid 密钥一起提交；重新加密已有连接不会清除内存中的绑定。
这两个修正通过构建生成的 BTC/SMP 覆层应用，不改写 vendored stack。
重复配置按完整字节比较跳过写入。CRC、截断、容量或 I/O 错误不会触发清空或
重新格式化；存储错误锁存到重启，禁止 RAM-only 成功。共用 NimBLE 应用和
TL3238 认证回调均检查错误并断开保存失败的链路。新增 NimBLE 应用应在认证
回调中检查 `ble_store_littlefs_ready()`，失败时断开并停止广播；部分 Host
版本不会把 store 写入错误向上层传递。BLS3 是一致性格式，不提供 Flash 静态加密。

NVS 每个 namespace 一个带 CRC 的 BNS1 文件，`open..close` 持事务互斥锁；
`set_blob` 修改快照，`commit` 原子替换，失败的 set 会使整次提交失败。支持
256/512 字节文件缓存，32 个 key、单值 4096 字节、文件 16384 字节。旧 LFS
EasyFlash API 文件 `/KV`、`/_ef4_kvs_` 仍可一次性导入，原文件保留。

各平台挂载只允许格式化确认全为 `0xff` 的空分区。Bouffalo 独立 SDK 工程通过
`../bouffalo_sdk/lfs_port/sdk_bridge.cmake` 复用 SDK PSM 挂载及分区信息。
TL3238 保持原 60 KiB LittleFS 分区；临时解码内存使用同一 FreeRTOS 堆。

## 验证

```sh
bash project/tl3238/tests/run_ble_store_tests.sh
bash component/ble_store_config/tests/run_tests.sh
# 不需要芯片 SDK、固件构建或硬件；直接测试两版 NimBLE + Bluedroid
python3 component/ble_store_config/tests/run_interop.py
```

测试入口只使用宿主 C 编译器、CMake、两个 vendored Host 和 LittleFS；同时运行
NimBLE 1.9 与 1.10。使用真实 Host 类型/适配器和合成密钥，覆盖旧 BLS2 导入、
NimBLE → Bluedroid → NimBLE 双向读取、SC/传统配对、地址字节序、IRK、签名计数、
配对提交、重复写入、删除后不复活、NVS 中不重复保存 LE key，以及已有绑定重连
不会被 SMP 清除。LittleFS/NVS 测试覆盖断电、I/O 错误、CRC/截断、缓存兼容和
OTA A/B/A 文件保留。存储测试启用 ASan/UBSan；为隔离上游 SMP 状态函数，其单独
编译单元不启用 sanitizer；当前环境不支持 LSan，未宣称通过泄漏检测。

TL3238 实机报告保存在 `build/ble-store-validation/`。真实密钥只存于权限受限的
`private/` 备份，不输出到日志，也不要提交版本库。

2026-11-14 最终验证：

| 平台 | 编译检查 | 硬件检查 |
|---|---|---|
| TL3238 | NimBLE 完整 CS/RAS/OTA/CoC、Bluedroid CoC | 原 iPhone 绑定加密重连、Host 来回切换、各双向 60 秒 CoC |
| BL618DG | NimBLE、Bluedroid CoC 固件 | 此轮未测 |
| AmebaD | NimBLE Host、配置及共用存储静态库 | 此轮未测 |

宿主交叉迁移测试在 NimBLE 1.9 / 1.10 均通过；LittleFS 和 NVS 分别通过
40 / 38 个断电切点。最终 SWS 回读确认旧 BLS2 文件、本地 IRK、原始双方
安全记录完全一致；Host 切换前后的 BLS3 根密钥及全部安全记录也一致。
恢复 2 个绑定，复位后存储错误和提交次数均为 0；旧 Bluedroid NVS 不再重复存 LE key。

无密钥材料的汇总见
[验证报告](../../build/ble-store-validation/unified-validation-summary.json)；
[最终 Flash 核对](../../build/ble-store-validation/unified-final-key-verification.json)。
