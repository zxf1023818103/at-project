# TL323X 独立 SDK 组件

TL3238 工程从这里取得芯片驱动、头文件、原厂库、启动代码和链接脚本。构建不再访问 `build/` 中的 SDK checkout，不加载原 SDK 的 CMake/Eclipse 工程，也不在构建时下载或解压 SDK。

| 目录 | 内容 |
| --- | --- |
| `platform/` | V4.1.0 TL323X 外设源码、`libdriver.a`、校准、板级初始化、裸机异常入口 |
| `ble/standard/` | 普通 BLE V4.0.4.8 的 TL323X 库、配套头文件、公共代码；包含 OTA 体积原型所需的外设示例 |
| `ble/cs/` | 用户提供的定制 CS Controller、配套 RF/协议头文件、CS 算法库及公共代码 |
| `startup/` | 独立维护的普通 BLE、CS 和纯平台启动/链接配置 |
| `tools/` | 工具链定位/安装、文件完整性和构建依赖审计 |

应用适配留在相邻的 [`telink_sdk_config`](../telink_sdk_config/)：FreeRTOS/tickless、NimBLE NPL、HCI、CS、OTA 和 LittleFS。NimBLE 1.10 的所需源码和 Linux/FreeRTOS NPL 已固定到 [`nimble_config/upstream-1.10.0`](../nimble_config/upstream-1.10.0/)，不再依赖下载缓存，其他工程的旧 NimBLE 子模块保持原用途。

`telink_sdk` 为 SDK 源码对象目标，`telink_sdk_headers` 提供所选版本的头文件和宏；`telink_driver_blob`、`telink_controller_blob`、`telink_cs_algorithm` 为本地库目标。原厂未提供源码的驱动、Controller 和算法实现仍以 `.a` 使用，并未替换为新的协议栈实现。

## 版本与配套关系

- 平台来源：`telink-semi/tl_platform_sdk`，`e88822226e4702910204e8599f44053065b68432`。
- 普通 BLE 来源：`telink-semi/tl_ble_sdk`，`b1edcb0b18b225f94da5eac45f7f4f8bafa6defd`。
- CS 来源：用户提供的 `tl_ble_sdk-custom-V4048_TL323X_CS.zip`，原归档 SHA-256 为 `840cd1aeb81f0ee7ce3f1dc1ec70ef8b08d42a5da0403c1d7010b719a94d415e`。
- 只提取 TL323X 及所需公共文件，未复制整个 SDK 的其他芯片库、工具链、IDE 工程、文档和示例集合。保留每个文件的版权声明和原厂许可清单 `LICENSES.upstream.txt`；`manifest.json` 记录来源路径和 SHA-256。

BLE 构建继续使用已经在实板验证的平台 V4.1.0 system/clock/PM 实现。原厂 Controller 库内重复的平台对象在构建副本中移除，RF 对象保留；RF 头文件和扩展驱动必须来自同一套普通/CS BLE 版本。平台驱动存在相对路径的 RF include，因此 CMake 在构建目录生成这一覆盖层，并在切换 CS 配置时清除旧版本残留文件。

`startup/*.S` 通过条件编译写入原生 OTA 头。BLE 链接脚本保留 2 KiB 启动栈、DRAM 末尾和 IRAM NOLOAD 边界检查，并从 `telink_sdk_config/storage/flash_layout.h` 取得 448 KiB 镜像限制。没有改动实板分区或写保护策略。

## 构建和审计

```sh
./project/tl3238/build.sh
./project/tl3238_hello/build.sh
make -C project tl3238_bootloader PROFILE=lean-smp-board-os

python3 component/telink_sdk/tools/verify_component.py \
  --build build/tl3238/firmware
```

只有编译器仍需外部安装；可以设置 `TL3238_TOOLCHAIN`。构建脚本不再使用 `TELINK_SDK_ROOT`、`TELINK_BLE_SDK_ROOT` 或 `TELINK_CS_SDK_ZIP`。旧构建缓存里的 NimBLE 路径会被更新为组件内副本。

审计同时检查已提取文件的 SHA-256、`compile_commands.json`、Ninja 构建图及编译器生成的头文件依赖，拒绝工程/组件/构建目录/工具链以外的输入。预编译库的 DWARF 可能显示原厂电脑上的源码路径，这不表示构建读取了那些文件。

`project/tl3238/compare_sdk_memory.py` 仍是显式重建原版 SDK 的历史对照工具；它不是应用构建依赖。小型 OTA 原型只复用该工具的 ELF 统计函数，实际源码和构建选项已来自本组件，不再解析原 SDK 工程。

迁移验证记录位于 `build/tl3238-component-migration/`。本轮进行编译和主机测试；迁移后的新镜像未烧录，早先的板端 OTA/无线测试结果对应原有固件 SHA。

实测全功能 O2/Os、无 CS/OTA/CoC/BLE 和 Hello 的 7 种配置均编译通过，8 个构建目录依赖审计通过，6 组已有主机测试通过。七种原厂 OTA 原型与拆分前 BIN 完全一致。CS 在同一目录 ON/OFF/ON 切换的覆盖层及最终 SHA 也通过检查。详见 [迁移汇总](../../build/tl3238-component-migration/summary.json)。
