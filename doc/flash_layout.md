# Flash Layout

各平台的 SPI NOR flash 布局。地址都按 raw offset 给出（mapped XIP address = raw + 平台 base，Ameba 系列 base = `0x08000000`）。

## Ameba D (RTL8721D / RTL8710C, 4 MB)

```
raw offset      size       region                          flash 保护
─────────────── ────────── ────────────────────────────── ───────────
0x000000        0x004000   km0_boot_all.bin     (16 KB)    🔒 protected
0x004000        0x002000   km4_boot_all.bin    ( 8 KB)    🔒 protected
0x006000        0x17D000   OTA1 image2          (1524 KB)  🔒 protected
0x183000        0x17D000   OTA2 image2          (1524 KB)  🔒 protected
─── 0x300000: flash 写保护边界（CMP=1, TB=0, BP=1MB → 上 1 MB unprotected）───
0x300000        0x0FA000   LittleFS             (1000 KB)  ✏️ writable
0x3FA000        0x001000   UART_SETTING_SECTOR  (   4 KB)  ✏️ writable
0x3FB000        0x001000   AP_SETTING_SECTOR    (   4 KB)  ✏️ writable
0x3FC000        0x003000   FTL_PHY_PAGE         (  12 KB)  ✏️ writable
0x3FF000        0x001000   FAST_RECONNECT_DATA  (   4 KB)  ✏️ writable
0x400000                   flash end
```

总计：24 KB boot + 2×1524 KB OTA + 1000 KB LFS + 24 KB settings = 4096 KB（4 MB 整）。

**设计取舍**
- OTA slot 各 `0x17D000` = 1524 KB（不是整 1.5 MB），让 firmware 段正好停在 `0x300000` (3 MB) 边界 —— 这个对齐让 flash status register 的 `CMP+TB+BP` 组合能以一次写入精确锁定下方 3 MB。
- LittleFS 紧贴 `0x300000`（保护边界），从这里到 `0x3FA000` 全部分给 LFS（1000 KB），剩余 24 KB 让给 settings cluster 放 flash 末尾。
- Settings cluster (UART/AP/FTL/Fast-reconnect) 沿用 SDK "Duer/Mesh" 分支同样的相对排布，仅基地址从 SDK 默认的 `0x1FA000` 平移到 `0x3FA000`（flash 末尾 24 KB）。FTL 占 3 个 4 KB sector（`ftl_phy_page_num = 3`），给蓝牙栈做 BLE static random address 等 KV。

### Flash 写保护

底部 3 MB 设为 read-only。机制：SR1 + SR2 组合写入 `CMP=1, TB=0, BP[2:0]=101`，使"top 1 MB"被 CMP 翻转成 unprotected 区，其下 3 MB 全部禁写。

具体寄存器值（W25Q32JV / GD25Q32 系列）：
- SR1 = `0x14` (TB=0, BP[2:0]=101, 其余 0)
- SR2 = 既有值 OR `0x40` (置 CMP=1，保留 QE 等其它位)

实现：[src/BoardFlashLock.cpp](../src/BoardFlashLock.cpp)，由 `app_pre_example()` ([src/main.cpp](../src/main.cpp)) 在 WLAN/BT/OTA 启动前调用 `at::lockFirmwareFlash()`。OTA 升级流程需要在写入前先调 `at::unlockFirmwareFlash()`，写完后重新 lock。

**未识别 flash 芯片** 走的是同一段代码（按 W25Q/GD25Q 协议发 0x35/0x31 命令）。MXIC（如 MX25L3206）SR layout 不同，需要单独处理 —— 当前未支持，遇到 MXIC SiP 会写错位但不会致命（解锁后旧 SR2 值保持）。

### 相关 SDK 改动

| 文件 | 改动 |
|------|------|
| [rtl8721d_bootcfg.c](../platform/ameba-rtos-d/component/soc/realtek/amebad/fwlib/usrcfg/rtl8721d_bootcfg.c) | `OTA_Region[1] = 0x08183000`（二阶 bootloader MMU 映射用） |
| [rtl8721d_ota.h](../platform/ameba-rtos-d/component/soc/realtek/amebad/fwlib/include/rtl8721d_ota.h) | `LS_IMG2_OTA2_ADDR = 0x08183000`（应用层 OTA API 用） |
| [platform_opts.h](../platform/ameba-rtos-d/project/realtek_amebaD_va0_example/inc/inc_hp/platform_opts.h) | UART/AP/FTL/Fast-reconnect 整体迁到 `0x3FA000-0x3FFFFF` |
| [platform_opts_auto_release.h](../platform/ameba-rtos-d/project/realtek_amebaD_va0_example/inc/inc_hp/platform_opts_auto_release.h) | 同上 |
| [littlefs_adapter.h](../platform/ameba-rtos-d/component/common/file_system/littlefs/littlefs_adapter.h) | `LFS_FLASH_BASE_ADDR = 0x300000, LFS_DEVICE_SIZE = 0x0FA000` |

`OTA_Region[1]` 和 `LS_IMG2_OTA2_ADDR` 两个值**必须严格同步**：前者是 boot ROM 启动时找 image2 用的，后者是应用层 OTA 升级写新 image 时找位置用的。任何一边没跟上，OTA 升级到 OTA2 后下次启动直接挂。

## Ameba Dplus (RTL8711Dx, 4 MB) / Ameba Green 2 (RTL8711F, 4 MB)

两个 SoC 走新 SDK [platform/ameba-rtos](../platform/ameba-rtos)，布局是硬编码在每个 SoC 自己的 `Flash_Layout[]` 数组里（不是 partition file 或 Kconfig 驱动）。bootloader 和应用层都通过 `flash_get_layout_info(region_type, &start, &end)` 查地址，**OTA 升级流程不像 Ameba D 那样有双源不一致风险**。

两个 SoC 用 **同一张地址表**：

```
raw offset      size       region                          flash 保护
─────────────── ────────── ────────────────────────────── ───────────
0x000000        0x010000   IMG_BOOT          (   64 KB)    🔒 protected
0x010000        0x170000   IMG_APP_OTA1      ( 1472 KB)    🔒 protected
0x180000        0x010000   IMG_BOOT_OTA2     (   64 KB)    🔒 protected
0x190000        0x170000   IMG_APP_OTA2      ( 1472 KB)    🔒 protected
─── 0x300000: 写保护边界（CMP=1, TB=0, BP=1MB → 上 1 MB unprotected）───
0x300000        0x100000   VFS1 / LittleFS   ( 1024 KB)    ✏️ writable
0x400000                   flash end
```

总：64 + 1472 + 64 + 1472 + 1024 = 4096 KB ✓

**设计取舍**

- IMG_BOOT slot 64 KB —— SDK 默认 Dplus 80 KB / Green 2 256 KB 都过大。实测 `km4_boot_all.bin` (Dplus) ≈ 31 KB / `amebagreen2_boot.bin` ≈ 29 KB，64 KB slot 仍有 ~30 KB 余量供 bootloader 升级。Green 2 由此每个 boot slot 省 192 KB（×2 = 384 KB）。
- IMG_APP_OTA × 2 各 1472 KB —— 比 Ameba D 的 1524 KB 略小（新 SDK 多了 IMG_BOOT_OTA2 这一块 64 KB），但仍宽裕：Dplus image 1337 KB → 135 KB 余量；Green 2 image 1143 KB → 329 KB 余量。
- VFS1 落在 `0x300000-0x3FFFFF`，整 1 MB，与 Ameba D 完全一致 —— 写保护边界都在 3 MB 处。
- **没有独立 FTL / settings 区**：新 SDK 的 BT FTL 在内部使用 VFS，WLAN fast-connect 已由 [src/main.cpp:42-48](../src/main.cpp#L42-L48) 接管成 no-op，不需要额外 sector。

### Flash 写保护

机制和 Ameba D 完全一致：`SR1 = 0x14` + `SR2 |= 0x40` ⇒ CMP=1, TB=0, BP[2:0]=101 ⇒ 顶 1 MB unprotected / 下 3 MB 写保护。

`src/BoardFlashLock.cpp` 在 `#elif defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2` 分支里实现，通过 `ameba_soc.h` 的 `FLASH_RxCmd` / `FLASH_SetStatus` + `flash_init_para.FLASH_cmd_wr_status2`（运行时拿到 chip 特定的 Write SR2 命令，自动适配 MXIC / W25Q / GD25）。

挂钩点是新 SDK 的 `_WEAK app_pre_example()`（在 KM4 main.c 早期调用，先于 WLAN/BT/OTA），由 [src/main.cpp](../src/main.cpp) 提供 strong override 调 `at::lockFirmwareFlash()`。

### 相关 SDK 改动

| 文件 | 改动 |
|------|------|
| [ameba_flashcfg.c (Dplus)](../platform/ameba-rtos/component/soc/usrcfg/amebadplus/ameba_flashcfg.c) | `Flash_Layout[]` 全部 5 条目重排成上表的地址 |
| [ameba_flashcfg.c (Green2)](../platform/ameba-rtos/component/soc/usrcfg/amebagreen2/ameba_flashcfg.c) | 同上，与 Dplus 完全一样 |

LFS 大小不用单独改 —— [littlefs_adapter.c:10-11](../platform/ameba-rtos/component/file_system/littlefs/littlefs_adapter.c) 里 `LFS_FLASH_BASE_ADDR` / `LFS_FLASH_SIZE` 是变量，在 [vfs.c:218-222](../platform/ameba-rtos/component/file_system/vfs/vfs.c) 里从 `flash_get_layout_info(VFS1, ...)` 动态填值，跟着 Flash_Layout 走。

## Bouffalo BL602 / BL616 / BL616CL (4 MB)

布局由每块板的 `partition_cfg_4M.toml` 驱动（[bsp/board/bl602dk/](../platform/bouffalo_sdk/bsp/board/bl602dk/config/partition_cfg_4M.toml) / [bl616dk/](../platform/bouffalo_sdk/bsp/board/bl616dk/config/partition_cfg_4M.toml) / [bl616cldk/config/partition_cfg_4M.toml](../platform/bouffalo_sdk/bsp/board/bl616cldk/config/partition_cfg_4M.toml)），三块板用同一份地址：

```
raw offset      size       region                          flash 保护
─────────────── ────────── ────────────────────────────── ───────────
0x000000        0x00E000   Boot2          (   56 KB)       🔒 protected
0x00E000        0x001000   PT0            (    4 KB)       🔒 protected
0x00F000        0x001000   PT1            (    4 KB)       🔒 protected
0x010000        0x178000   FW slot 0      ( 1504 KB)       🔒 protected
0x188000        0x178000   FW slot 1      ( 1504 KB)       🔒 protected
─── 0x300000: 写保护边界（CMP=1, TB=0, BP=1MB → 上 1 MB unprotected）───
0x300000        0x100000   PSM (LittleFS) ( 1024 KB)       ✏️ writable
0x400000                   flash end
```

总：56 + 4 + 4 + 2×1504 + 1024 = 4096 KB ✓

**设计取舍**
- 砍掉 SDK 默认的 mfg / media / DATA / factory / KEY 五个分区。前两个对我们的 AT 命令固件没用（mfg 是定频测试 fw，media 是 ROMFS 资源），后三个整个 SDK grep `"KEY" / "DATA" / "factory"` 都没人引用，是历史占位
- 双 FW slot 对称 1504 KB，OTA 升级容量足够：BL602/616/616CL 当前 image 均 < 1 MB，余量 > 600 KB
- BL616 / BL616CL 用 v2 dynamic mode，Boot2 进 PT 可 OTA；BL602 v1 legacy，Boot2 固定在 `0x0`，不进 PT
- BL602 v1 toml 简洁（无 Boot2 / version 字段），BL616 / 616CL v2 toml 含 Boot2 entry + header/security flags

### EasyFlash = LittleFS over PSM

新 SDK 的 EasyFlash 接口（`ef_set_env_blob` / `ef_get_env_blob` / `ef_del_env`）已经**全部走 LittleFS 后端**，实现在 [components/fs/littlefs/easyflash_port/lfs_easyflash.c](../platform/bouffalo_sdk/components/fs/littlefs/easyflash_port/lfs_easyflash.c)。`lfs_xip_init({.partition_name = "PSM"})` 通过 MTD 按名字找 PSM 分区地址，所以 **toml 里 PSM 大小一改，LFS 跟着变，应用代码不动**。KV 数据落在 LFS 文件系统的 `/_ef4_kvs_/` 目录。BT 栈也走这条 API。

**注意**：PSM 从默认 32 KB 扩到 1 MB，LFS 检测几何变更会触发重格式化，**已有 KV 数据全部清空**。

### Flash 写保护

机制和 Ameba 完全一致：`SR1 = 0x14` + `SR2 |= 0x40` ⇒ CMP=1, TB=0, BP[2:0]=101 ⇒ 顶 1 MB unprotected / 下 3 MB 写保护。

[src/BoardFlashLock.cpp](../src/BoardFlashLock.cpp) 的 `#elif defined AT_UART_BACKEND_BFLB` 分支用 LHAL 的 `bflb_sflash_read_reg` / `bflb_sflash_write_reg`（在 [drivers/lhal/src/flash/bflb_sflash.h](../platform/bouffalo_sdk/drivers/lhal/src/flash/bflb_sflash.h)），`reg_index = 0/1` 分别对应 SR1/SR2（由 `flash_cfg->{read,write}_reg_cmd[]` 表驱动，跨 SoC 通用）。SR1+SR2 用 16-bit 原子写（cmd 0x01，2 字节），抄自 `bl602_sflash_ext.c:596` 的范式。

调用时机：[src/main.cpp](../src/main.cpp) 的 BL `main()` 在 `board_init()` 之后、`bflb_mtd_init()` + `easyflash_init()` 之前调 `at::lockFirmwareFlash()`，保证 flash driver 已经初始化 + 没有任何 FW 写入操作发生。

`unlockFirmwareFlash()` 仅作 OTA 流程预留，**当前不在任何地方调用**。

### 相关 SDK 改动

| 文件 | 改动 |
|------|------|
| [partition_cfg_4M.toml × 3](../platform/bouffalo_sdk/bsp/board/) | 整体重写：删 mfg/media/KEY/DATA/factory，PSM 扩到 1 MB @ `0x300000`，FW slot 对称 `0x10000 + 0x178000` / `0x188000 + 0x178000` |

构建脚本（[project/bouffalo/tools/build_whole.py](../project/bouffalo/tools/build_whole.py)）按 GLOB 取唯一一份 `partition_cfg_*.toml`，不用动。
