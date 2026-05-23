---
name: reference-amebad-flash-layout
description: "Ameba D (RTL8721D) 4 MB flash partition map, SDK gotchas around OTA / FTL / LFS defaults, and how firmware write-protect is wired"
metadata: 
  node_type: memory
  type: reference
  originSessionId: 1686892a-3cdc-44f9-ba09-65773131dc6e
---

The canonical layout lives at [doc/flash_layout.md](/home/zxf/at-project/doc/flash_layout.md). Below are the **non-obvious things** that doc doesn't make screamingly explicit and that bite if you only read the SDK.

## Two parallel OTA address sources that must agree

The Ameba D SDK has the OTA2 base address hard-coded in **two** places. Both have to point at the same slot or boot will fail after a successful OTA-to-OTA2 update:

- [rtl8721d_bootcfg.c](/home/zxf/at-project/platform/ameba-rtos-d/component/soc/realtek/amebad/fwlib/usrcfg/rtl8721d_bootcfg.c): `OTA_Region[2] = {0x08006000, 0x08...}` — used by the **second-stage bootloader** at startup to MMU-map image2.
- [rtl8721d_ota.h](/home/zxf/at-project/platform/ameba-rtos-d/component/soc/realtek/amebad/fwlib/include/rtl8721d_ota.h): `LS_IMG2_OTA1_ADDR` / `LS_IMG2_OTA2_ADDR` — used by **application-level OTA** code when writing a new image.

Touch one, you must touch the other. After OTA succeeds, `OTA_Change(idx)` flips active slot and reboots; the boot ROM then uses `OTA_Region[idx]`, NOT the macros from `rtl8721d_ota.h`.

## SDK defaults overlap each other on 4 MB flash

Out of the box (before patching):
- `LS_IMG2_OTA1_ADDR = 0x08006000`, `LS_IMG2_OTA2_ADDR = 0x08106000` — both 1 MB slots, packed tight
- LittleFS default `LFS_FLASH_BASE_ADDR = 0x200000, LFS_DEVICE_SIZE = 0x100000` ([littlefs_adapter.h](/home/zxf/at-project/platform/ameba-rtos-d/component/common/file_system/littlefs/littlefs_adapter.h)) — **overlaps OTA2's first 24 KB** (OTA2 ends at `0x206000`)
- UART/AP/FTL/FastReconnect default `0x100000-0x105FFF` ([inc_hp/platform_opts.h](/home/zxf/at-project/platform/ameba-rtos-d/project/realtek_amebaD_va0_example/inc/inc_hp/platform_opts.h) else-branch) — **inside OTA1 region**; gets wiped any time OTA1 is written

These conflicts are real; the SDK examples that ship LFS just don't enable OTA, and vice versa. Our patched layout (in [doc/flash_layout.md](/home/zxf/at-project/doc/flash_layout.md)) fixes both by carving the top 1 MB explicitly: LFS at `0x300000`, settings cluster at `0x3FA000-0x3FFFFF`, OTA each grown to 1524 KB.

## `flash_get_status2()` is declared but not linked

[flash_api.h:117](/home/zxf/at-project/platform/ameba-rtos-d/component/common/mbed/hal_ext/flash_api.h#L117) declares `int flash_get_status2(flash_t*)` but it's NOT implemented in any `.c` in Ameba D's SDK. Calling it gives a linker error. To read SR2, go raw:

```cpp
extern "C" {
#include "rtl8721d_flash.h"  // declares FLASH_RxCmd
}
uint8_t sr2 = 0;
FLASH_RxCmd(0x35, 1, &sr2);  // 0x35 = read SR2 on W25Q/GD25
```

(`flash_set_status` IS implemented but it only writes SR1; it READS existing SR2 and re-writes it unchanged. To change CMP/QE bits you go raw with `FLASH_SetStatus(0x31, 1, &val)` directly.)

## FTL size is hard-coded to 3 pages

`ftl_phy_page_num = 3` in [rtl8721dhp_intfcfg.c:49](/home/zxf/at-project/platform/ameba-rtos-d/component/soc/realtek/amebad/fwlib/usrcfg/rtl8721dhp_intfcfg.c#L49). FTL therefore always occupies `FTL_PHY_PAGE_START_ADDR` + 12 KB. Changing the count requires re-flashing — FTL re-formats if the layout changes, losing all KV data.

FTL is compiled into firmware whenever `CONFIG_BT_EN=y` (see [project_hp/asdk/make/file_system/Makefile:22](/home/zxf/at-project/platform/ameba-rtos-d/project/realtek_amebaD_va0_example/GCC-RELEASE/project_hp/asdk/make/file_system/Makefile)). Our project has BT on, so FTL is always linked.

## Flash write-protect implementation

`at::lockFirmwareFlash()` in [src/BoardFlashLock.cpp](/home/zxf/at-project/src/BoardFlashLock.cpp) writes `SR1=0x14, SR2|=0x40` ⇒ `CMP=1, TB=0, BP[2:0]=101` ⇒ "top 1 MB unprotected, bottom 3 MB protected" on W25Q32JV / GD25Q32 family. MXIC chips need different bit layout — not handled.

It's invoked from `app_pre_example()` ([src/main.cpp](/home/zxf/at-project/src/main.cpp)) which the SDK runs before WLAN/BT/OTA come up. **OTA code path needs to call `unlockFirmwareFlash()` before erase/write and re-lock after** — we don't have OTA flow yet, so currently nothing unlocks.

If you brick yourself by mis-writing SR: connect via JLink/DAP and issue `flash_reset_status` (writes 0 to SR1+SR2 = full unlock), or `chip_erase` + reflash.

## LOGUART baud changes after boot

Bootrom / floader uses **115200** on LOG_TX/LOG_RX (that's where the `0x15` download-mode handshake happens, see [[reference-amebad-imagetool]]). **Once the app boots, it switches LOGUART to 1.5 Mbps** for normal logging. So:

- Sniff for download-mode `0x15` → 115200
- Sniff for app `printf` output → 1500000

If you sniff app output at 115200 you get garbage (~1 byte/sec of corrupted noise) and might think the chip is silent / hung. Verified 2026-05-20 — output was clean as soon as the sniff was switched to 1.5 Mbps.

## SDK KV (`rt_kv_*`) prints spurious "failures"

[component/common/file_system/kv/kv.c](/home/zxf/at-project/platform/ameba-rtos-d/component/common/file_system/kv/kv.c) is naive:
- `rt_kv_init()` unconditionally calls `lfs_mkdir("KV")` and prints `mkdir kv failed, mayed already exist!!!!` whenever the dir is already there — which is *always* if you've baked `KV/` into your `lfs.bin`. Skip `rt_kv_init()` if KV is preloaded; `rt_kv_set/get` do their own `lfs_mount`.
- `rt_kv_get(key, buf, len)` checks `if (lfs_file_read(...) != len)` and prints `read failed, err is N` where N is the *actual bytes read*. Pass `len` equal to the real value length (not `sizeof(buf)`) to dodge the false positive on short reads.

Worked around in `src/TestTask.cpp::testFileSystem1()` rather than patching the submodule.
