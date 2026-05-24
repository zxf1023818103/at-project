# LittleFS

三方平台共用同一份 littlefs，约定 **同一份 lfs 镜像可以烧到任一平台**。本文档
记录使这件事成立所需的全部参数，并把三家平台跟上游公版的差异列清楚。

## 跟上游公版的关系

三家 SDK 里 lfs.h 的 `LFS_VERSION` 宏都是 `0x00020008`（即"2.8.0"），但实际
源码包含了上游 v2.8.1 / v2.8.2 的两个补丁，所以本仓库 `component/littlefs/`
submodule 直接 **pin 在 v2.8.2** —— 这是跟三家 SDK 实际行为最匹配、vendor
补丁面积最小的对齐点。

从 v2.8.2 出发，剩下的差异如下（基于 `diff -u` 实测，全部限定在
`lfs.c` / `lfs.h` / `lfs_util.h` 三个文件，`lfs_util.c` 三家无修改）：

### Bouffalo + Realtek 两家都打的补丁（vendor 共识）

- **`strcpy(info->name, "...") → strlcpy(info->name, "...", sizeof(info->name))`** —
  `lfs.c` 里 3 处往 `lfs_info::name`（定长缓冲）写常量字符串的地方。源串本身
  都很短（`"/"`, `"."`, `".."`），功能上等价；纯粹是改成防御性写法，跟磁盘
  格式无关。
- **`\n` → `\r\n`** —— `lfs_util.h` 里的 `LFS_TRACE` / `LFS_DEBUG` /
  `LFS_WARN` / `LFS_ERROR` 四个宏共 16 行字符常量。串口 terminal 的 CRLF
  约定，跟磁盘格式无关。

### Realtek 独有的补丁（Bouffalo 没有）

`lfs.h`：
- 新增 `LFS_ERR_NOTFMT = -10` 枚举值。

`lfs.c`：在 `lfs_dir_fetchmatch()` 扫盘逻辑里加了"全 `0xff` 擦除态"探测，
约 6 行：
- 函数开头声明 `uint32_t fmt_flag = 0;`
- 扫到 `tag == 0xffffffff` 时把 `fmt_flag = 1`
- 整段扫描结束如果只见到擦除态，就 `return LFS_ERR_NOTFMT;` 而不是
  原本的"目录损坏"错误

让 `lfs_mount()` 能区分"空白介质"和"真损坏"两种情况，上层可以据此走自动
格式化路径。

### 平台间冗余

`platform/ameba-rtos/` 和 `platform/ameba-rtos-d/` 里的 lfs 源码 **逐字节
相同**（同一份 Realtek 拷贝），跟 `platform/bouffalo_sdk/` 的差别只有上面
"Realtek 独有补丁"那 6+1 行。Bouffalo 没有 `LFS_ERR_NOTFMT`，因为它的
adapter 走自己的"FNF 自动 format"逻辑。

### 我们这边怎么处理

`component/littlefs/` submodule 锁在 v2.8.2，**不上游补丁、不 fork**。Vendor
那点 strlcpy / CRLF / NOTFMT 差异都不影响磁盘格式（`disk version` 仍是 2.1），
所以 mount / 读写其它平台写出来的镜像完全兼容。需要 Realtek 那个"空白介质
自动 format" 行为时，由 adapter 层 wrap `lfs_mount` 的返回值，自己判断
"corrupt + 全 ff" 来触发，避免改动公版源码。

## 镜像几何（image geometry）

所有平台都用 4 MB SPI NOR flash，LFS 区严格统一：

| 字段 | 值 | 备注 |
|---|---|---|
| 起始 raw offset | `0x300000` | 3 MB 处 —— 跨平台一致，也是 flash 写保护边界 |
| 大小 | **1000 KB** (`0xFA000`) | 跟 Ameba D 的 layout 强制对齐（D 的 LFS 后面挤了 24 KB settings sector，没法扩到 1024 KB）|
| block_size | 4096 | NOR sector |
| block_count | 250 | = 1000 KB / 4 KB |
| disk version | 2.1 | littlefs v2.8.0 默认 `LFS_DISK_VERSION = 0x00020001` |

具体每个平台的全 flash layout 见 [flash_layout.md](flash_layout.md)。Dplus / Green2 /
Bouffalo 三家在 LFS 区末尾各留 24 KB 不分配，给"将来某天可能要加 settings
sector"做缓冲；不留也不影响功能。

## 运行时 lfs config（lfs_config 结构体）

三方 SDK 的 LFS adapter 都按下表配置 NOR 路径：

| 字段 | 值 | 出处 |
|---|---|---|
| `read_size` | 256 | [bouffalo_sdk lfs_easyflash.c:31](../platform/bouffalo_sdk/components/fs/littlefs/easyflash_port/lfs_easyflash.c), [ameba-rtos littlefs_adapter.c:188](../platform/ameba-rtos/component/file_system/littlefs/littlefs_adapter.c), [ameba-rtos-d littlefs_adapter.c:18](../platform/ameba-rtos-d/component/common/file_system/littlefs/littlefs_adapter.c) |
| `prog_size` | 256 | 同上 |
| `block_size` | 4096 | 同上 |
| `cache_size` | 512 | 同上 |
| `lookahead_size` | 256 | 同上 |
| `block_cycles` | 500 | 同上 |

`read_size`、`prog_size`、`block_size` 跟磁盘格式绑定，**改这三个值会让现有镜像
不可读**。`cache_size`、`lookahead_size`、`block_cycles` 是 RAM / 行为参数，
不写盘，可以各平台不同（但还是统一以保持简单）。

## 生成镜像：mklfs

工具：[platform/bouffalo_sdk/tools/genlfs/mklfs-ubuntu](../platform/bouffalo_sdk/tools/genlfs/mklfs-ubuntu)
（macOS 用 `mklfs-macos`，Windows 用 `mklfs.exe`），跟运行时一样底层
littlefs v2.8.0。

命令模板（跨平台统一）：

```sh
mklfs-ubuntu \
  -c <input_dir> \
  -b 4096 \
  -r 256 \
  -p 256 \
  -s 1024000 \
  -i <output.bin>
```

- `-s 1024000` 字节 = 1000 KB
- mklfs 自己会把 `cache_size = prog_size = 256`、`lookahead_size = ALIGN8(block_count >> 4)`
  ≈ 16 算出来；这两个不写盘，跟运行时 512 / 256 的差异不影响 mount

项目里的封装：`make -C project lfs_image` 会用上面这套参数从 `project/resource/`
打包出 `project/lfs.bin`。`resource/` 的内容直接成为 lfs 镜像的根 —— 见
[kv.md](kv.md) 关于 `resource/KV/<key>` 怎么变成运行时 `ef_get_env_blob /
rt_kv_get` 能读的 KV。

## 嵌入到合成固件

每个平台的合成固件（whole_flash_data.bin / Image_All.bin）已经把 lfs 区那
1000 KB 一起打包，烧机后开机就能用 —— 不再是空白擦除态。

- **Bouffalo**：[tool/bflb_elf_to_bin.py](../tool/bflb_elf_to_bin.py)
  把 PSM 当作普通 partition 段交给 `BLFlashCommand --build`
- **Ameba D**：[project/amebad/Makefile](../project/amebad/Makefile) 用 `dd` 把 `../lfs.bin` 拼到 `seek=0x300000`
- **Ameba Dplus / Green2**：[project/ameba/Makefile](../project/ameba/Makefile) 同上

验证 lfs 段真的被嵌进去了（CI 自动跑，本地也可手动）：

```sh
# 拿 project/lfs.bin 的字节数（K）跟合成固件 0x300000 起的字节比对
LFSSZ=$(stat -c %s project/lfs.bin)
cmp -n $LFSSZ project/lfs.bin <(dd if=<merged.bin> bs=1 skip=$((0x300000)) count=$LFSSZ status=none)
```

`cmp` 0 退出 = 字节级一致。
