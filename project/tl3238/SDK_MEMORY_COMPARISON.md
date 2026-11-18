# TL3238 与原版 CS SDK 的内存比较

2026-11-14，比较对象为用户提供的 `tl_ble_sdk-custom-V4048_TL323X_CS.zip`。当前优化版固件比原厂“单天线＋RAS”Initiator 多预留 **36,160 B（35.31 KiB，约 29.0%）RAM**。这是完整应用的链接预留差异，包含不同大小的内存池，不能视作 NimBLE Host 自身的开销。

## 实际编译结果

全部使用本机 Telink GCC 12.2.0。原厂配置从 Eclipse `.project` / `.cproject` 读取，保留源文件排除规则、宏、`-O2`、LTO、链接脚本及库顺序；Reflector 原有的 `-fpack-struct` 也保留。

| 工程 | IRAM（B） | DRAM（B） | RAM 合计（B / KiB） | 合计中包含的内存池（B） | BIN（B） |
| --- | ---: | ---: | ---: | ---: | ---: |
| 原厂默认 Initiator | 86,344 | 43,080 | 129,424 / 126.39 | SDK Host 4,096 | 332,189 |
| 原厂默认 Reflector | 81,480 | 17,768 | 99,248 / 96.92 | SDK Host 4,096 | 261,770 |
| 原厂单天线＋RAS Initiator | 88,648 | 35,936 | 124,584 / 121.66 | SDK Host 9,706 | 346,757 |
| 原厂单天线＋RAS Reflector | 80,968 | 12,704 | 93,672 / 91.48 | SDK Host 7,836 | 282,070 |
| 当前 FreeRTOS＋NimBLE＋CS/RAS＋mcumgr | 97,584 | 63,160 | 160,744 / 156.98 | FreeRTOS heap 38,912 | 405,730 |

1 KiB = 1024 B。RAM 包含驻留代码、retention data、初始化和未初始化数据、完整静态内存池及布局对齐空隙；**不含 `_end` 以上的启动/裸机运行栈空间**。BIN 是 `objcopy -O binary` 生成的文件长度，单列反映 Flash 镜像大小，不用于 RAM 统计。

统计使用 ELF 的实际 `ALLOC` 段：IRAM 为所有 IRAM 段与已导出结束符号的最大末地址减 `0x68000`；DRAM 为 `_end - 0x80000`。原厂 ELF 没有导出 `_IRAM_BSS_VMA_END`，仅统计 `_RAMCODE_VMA_END` 会漏掉后面的 **4,608 B `print_fifo_b` 及 4 B 对齐**。上表已包含它，也包含 DRAM 末尾的 `.sdk_version`。初步统计曾漏计这段日志空间，上表及最终 JSON 已修正。

当前布局为 96 KiB IRAM＋64 KiB DRAM。原厂链接检查以 `_BSS_VMA_END` 为界至少留出 600 B 栈间隙，随后还有 `.sdk_version`，这不是实测栈峰值。当前固件以完整 `_end` 为界留出 2,048 B 启动栈，另有已计入 IRAM 的 2,048 B ISR 栈；FreeRTOS 任务栈从上表的 38 KiB heap 分配，不能再次累加。

## 配置差异与内存池

| 项目 | 原厂默认 | 原厂单天线＋RAS | 当前固件 |
| --- | --- | --- | --- |
| 系统 | 裸机 | 裸机 | FreeRTOS 11.1.0＋tickless |
| Host | Telink | Telink | NimBLE 1.10 |
| 天线 / 路径 | 2 / 4 | 1 / 1 | 1 / 1 |
| 测距数据传输 | Inline-PCT，RAS 关闭 | Inline-PCT 关闭，RAS 开启 | 基于 NimBLE 的按需 RAS |
| 角色 | Initiator / Reflector 分开构建 | Initiator / Reflector 分开构建 | 同一固件支持两种角色，Host 最多 1 个连接 |
| 每过程记录容量 | 8 subevent / 106 step 的 SDK 配置 | 8 subevent / 106 step | 32 subevent / 256 step |
| 解算 | Initiator 启用原厂 Algorithm 4 | Initiator 启用原厂 Algorithm 4 | 单天线 Mode 2 PBR 相位拟合 |
| 附加功能 | 原厂调试/UI | 原厂调试/UI | UART CLI、mcumgr、RTOS 队列/FPU/定时器验证任务 |

“单天线＋RAS”仅在构建目录的 SDK 副本中修改两份应用配置：`APP_IPT_SUPPORT_EN=0`、`APP_RAS_SUPPORT_EN=1`，选择单天线板型 `BOARD_323X_EVK_C1T388A20`，并明确设置 `NUM_ANT_SUPPORT=1`、`MAX_ANT_PATHS_SUPPORT=1`、`ANTENNA_SWITCHING_AUTO_EN=0`。原厂默认源码不修改，未裁减原厂调试缓冲、算法或功能。

原厂两个应用均为 `FREERTOS_ENABLE=0`，ELF 没有 `ucHeap`；头文件里的 `configTOTAL_HEAP_SIZE=16*1024` 在这些构建中不生效。实际 SDK Host 池是 `g_cs_app_host_malloc_buffer`。RAS 单天线每份过程数据容量 1,870 B，Initiator 池为 `3×1870+4096=9706 B`，Reflector 为 `2×1870+4096=7836 B`。

以原厂单天线 RAS Initiator 为参照，可以把链接预留差额作如下算术拆分：

| 项目 | 原厂（B） | 当前（B） | 增量（B） |
| --- | ---: | ---: | ---: |
| Host 池 / FreeRTOS heap | 9,706 | 38,912 | 29,206 |
| 其余 RAM 预留 | 114,878 | 121,832 | 6,954 |
| 合计 | 124,584 | 160,744 | 36,160 |

两种池承担的职责不同：原厂 RAS 记录放在 Host 池里，当前 RAS 本地/远端记录位于独立静态缓冲；当前 heap 还承担任务栈、内核对象和动态分配。因此 **6,954 B 也不是 NimBLE 的净增加量**。当前保留两个角色的 Controller TX 缓冲，以及 HCI 桥接、NimBLE 包池和 mcumgr 缓冲；原厂 Initiator 则有当前未使用的 FFT/Algorithm 4 工作区。当前沿用定制 CS Controller/RF，但平台启动与普通外设来自适配过的 V4.1 平台，原厂基线使用压缩包自带平台。两套算法的功能和无线精度也不能由内存比较判定等价。

## 运行时余量

当前固件先前已通过板端本地回归，报告哈希与本次比较的 BIN 一致：heap 容量 **38,912 B**，测试结束空闲 **17,232 B**，AES/P-256 自检期间最低空闲 **15,280 B**，最大连续空闲块 **17,136 B**。这些空闲字节已经包含在 160,744 B 链接预留中，并不表示所有预留字节都在使用。

当前 IRAM 还有 720 B 链接余量；DRAM 在扣除 2 KiB 启动栈后还有 328 B 未分配空间。FreeRTOS 只能直接从其已配置 heap 分配，未分配地址空间和 heap 空闲是不同指标。

四个原厂版本均仅编译、链接并检查 ELF/BIN，**未烧录，未测原厂运行时池空闲和栈峰值**。当前板子无天线，已有报告也是本地验证，尚不能比较真实无线连接、配对及 CS/RAS 测距时的峰值用量。本次没有更改或烧录当前固件。

## 复现与记录

在仓库根目录运行，依次构建以避免同时改写 RAS 副本配置：

```sh
python3 project/tl3238/compare_sdk_memory.py --role initiator --variant default
python3 project/tl3238/compare_sdk_memory.py --role reflector --variant default
python3 project/tl3238/compare_sdk_memory.py --role initiator --variant ras-single
python3 project/tl3238/compare_sdk_memory.py --role reflector --variant ras-single
python3 project/tl3238/compare_sdk_memory.py --summary
```

脚本支持 `--sdk`、`--toolchain`、`--output` 覆盖路径；汇总时可用 `--current-elf`、`--verification` 指定当前固件及对应板端报告。没有板端报告时只汇总静态结果；已有报告的固件哈希不匹配或结果非 PASS 时拒绝使用。配置数值由独立、未链接入固件的编译探针提取。

输出位于 `build/tl3238/sdk-memory-comparison/`。每个配置保留 ELF、BIN、map、编译/链接日志、源文件哈希、完整编译参数、链接命令及 `memory.json`；总表为 [`comparison.json`](../../build/tl3238/sdk-memory-comparison/comparison.json)。本次逐文件检查压缩包中的 1,475 个源文件、头文件、库、链接脚本和 Eclipse 配置，与原厂目录一致；五份 ELF 重新导出 BIN 均与被比较的文件逐字节相同。原厂构建中的现有警告保留，编译成功不表示已经验证板端启动或无线功能。

| 文件 / 构建 | SHA-256 |
| --- | --- |
| 用户提供的 SDK ZIP | `840cd1aeb81f0ee7ce3f1dc1ec70ef8b08d42a5da0403c1d7010b719a94d415e` |
| 原厂 `liblt_TL323X.a` | `8fc4dcf1fdf35650f4bddc8b0667a6763dc8b8529474adb402e42524ab7f9c5a` |
| 默认 Initiator BIN | `521fe73a938c213912449b9613c57737ae7af667c438b1ee801dfd58fd39e5a7` |
| 默认 Reflector BIN | `bb1becf236635f9df96165dad717d50bbec5d5d4e525cb67db80ecf925963705` |
| 单天线 RAS Initiator BIN | `874f4eb749cb7b83e97345cfcfb9a5b781612a93c15981dc4d3d24c9c2adf9eb` |
| 单天线 RAS Reflector BIN | `fb489ba5b2a98d94dafad008092abb131b479e6a425cee3e08dac520230547fe` |
| 当前 BIN | `6094e778e9ab9dcf0b8bcb86c10762b873893a98996a6bb0e39762d5c16435d0` |
