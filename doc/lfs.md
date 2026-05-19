# LittleFS

三方平台共用同一份 littlefs（v2.8.0 + Realtek `LFS_ERR_NOTFMT` 补丁），并约定
**同一份 lfs 镜像可以烧到任一平台**。本文档记录使这件事成立所需的全部参数。

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

- **Bouffalo**：[project/bouffalo/tools/build_whole.py](../project/bouffalo/tools/build_whole.py)
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
