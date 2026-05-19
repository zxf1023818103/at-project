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

## Ameba Dplus / Green 2

待补 —— 这两个 SoC 走 `platform/ameba-rtos` 的 partition system（不是 Ameba D 的硬编码地址），布局由 [project/ameba/prj.conf](../project/ameba/prj.conf) + `flash_xxx.json` 控制。当前默认布局没改过，OTA 默认 1 MB / slot。

## Bouffalo BL602 / BL616 / BL616CL

待补 —— Bouffalo 由 `partition_cfg_*.toml` 决定，每块板的分区表在 `platform/bouffalo_sdk/bsp/board/<board>/config/`。
