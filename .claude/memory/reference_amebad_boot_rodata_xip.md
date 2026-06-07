---
name: amebad-boot-rodata-xip-trap
description: AmebaD driver-only bootloader at -Os orphans .rodata strings into XIP flash → km0/km4 boot hangs before image2; fix = *(.rodata*) in boot ld .ram_image1.text
metadata: 
  node_type: memory
  type: reference
  originSessionId: 2c7314f2-7f7e-46e5-84f0-75d717be0da0
---

The CMake driver-only AmebaD bootloaders hang very early on real hardware — LOG_TX goes all-0x00, the chip never reaches image2 (no `[KM4]` banner, none of the SDK's `Flash ID / FLASH CALIB / WIFI init`).

**Root cause:** at `-Os` GCC merges the boot's string literals (the `DBG_8195A` format strings) into `.rodata.str1.1`. The boot linker scripts' `.ram_image1.text` only catches `*(.boot.ram.text*)` + `*(.boot.rodata*)`, NOT generic `*(.rodata*)` — so those strings get orphaned into the XIP flash region (`.rodata.str1.1` ends up around `0x080005E0`). km0_boot / km4_boot run from RAM (BOOTLOADER_RAM / BD_RAM) before XIP is usable, so dereferencing the orphaned strings faults → hang before it can load/jump to image2.

**Fix:** add `*(.rodata*)` to `.ram_image1.text` in both
`component/amebad/bootloader_km0/rlx8721d_boot.ld` and
`component/amebad/bootloader_km4/rlx8721d_img1_s.ld` (CMAKE_PATCH; the bootloader
folders were later flattened — ld scripts now sit at the folder root, no `ld/`
subdir). After the fix:
km0_boot `.ram_image1.text` 1048→1897, `km0_boot_all.bin` 3244→**4096** (matches the SDK's `image/km0_boot_all.bin` exactly); `km4_boot_all.bin` → **4256**.

**GOTCHA / correction:** the earlier "loader axf byte-identical to SDK" claim (size `0xfbd`) was WRONG — our `km0_boot_all.bin` was 3244 vs the SDK's 4096 (849 B of RAM rodata missing). When validating amebad boot, compare the actual `*_boot_all.bin` / `.ram_image1.text` sizes against the SDK's `project_lp/asdk/image/` & `project_hp/.../image/` outputs, not just the axf size.

**Diagnosis recipe (no JTAG):**
1. Assemble a known-good reference from the SDK's pre-built bins (`project_lp/asdk/image/{km0_boot_all,km0_km4_image2}.bin` + `project_hp/.../image/km4_boot_all.bin`) at flash offsets `0x0 / 0x4000 / 0x6000`. Flash + sniff LOG @ **1500000** — it boots clean. (This also proves board/flash/read-path and that the blanked DPK/BACKUP/XIPSYS gap is fine.)
2. Mix-and-match to isolate: SDK boots + our image2 (→ our image2 is OK), our km0_boot + SDK km4_boot (→ km0_boot bad), etc.

**LOG baud:** ROM/early-boot is 115200; KM0 image2 (`LOGUART_SetBaud(1500000)`) switches to **1.5 Mbps**, so the app log + our `[KM4]` banner read clean at 1500000, garbled/zeros at 115200.

Tools: `component/amebad/image_tool/build_image_all.py` (build both cores + cross-core merge + assemble `Image_All.bin`), `project/amebad/sniff_log.ps1` (single-port reset + LOG dump). Flash via [[reference_amebad_imagetool]]; flash layout in [[reference_amebad_flash_layout]].
