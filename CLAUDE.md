# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an AT command firmware project targeting multiple embedded IoT SoC platforms. All platforms share a single source file (`src/main.cpp`) that uses heavy `#ifdef` guards to conditionally compile platform-specific initialization, Wi-Fi, and Bluetooth LE setup code. Each platform has its own build project under `project/`.

## Supported Platforms and Build Commands

Platform SDKs are managed as git submodules under `platform/`. Run `git submodule update --init --recursive` after cloning.

### Bouffalo Lab BL602 / BL616 / BL616CL
- SDK: `platform/bouffalo_sdk` (CMake + ninja via `project.build`)
- Toolchain: `platform/toolchain_gcc_t-head_linux` (RISC-V, added to PATH automatically)
- Project: `project/bouffalo/` is shared across all three chips; CHIP/BOARD select the target
- Build: `cd project/bouffalo && make CHIP=<bl602|bl616|bl616cl> BOARD=<bl602dk|bl616dk|bl616cldk>`
- Output: `project/bouffalo/build_<CHIP>/build_out/whole_flash_data.bin`
- Config: `project/bouffalo/proj.conf` (CMake, chip-conditional), `project/bouffalo/CMakeLists.txt`
- Flash layout: derived from `platform/bouffalo_sdk/bsp/board/<BOARD>/config/partition_cfg_*.toml`; `project/bouffalo/tools/build_whole.py` reads it and drives `BLFlashCommand --build` to produce `whole_flash_data.bin`
- Security/eFuse: the chip's eFuse security-field inventory (SF-AES flash encryption / RSA-ECC secure boot / ROTPK-hash / key slots / debug locks + Sec_Eng/PKA/TZC), with an AmebaD cross-reference: `doc/bouffalo_keys.md`
- ROM symbols: the per-chip boot-ROM API inventory (fixed function-pointer table + index; BL602 `RomDriver_*` @0x21010800 / BL616 `ROM_API_INDEX_*` @0x90015800 / BL616CL @0x90019000; E907 chips add a `romapi_patch` layer), module breakdown, and how it relates to the crypto path (TRNG/AES/SHA/PKA do NOT go through ROM): `doc/bouffalo_rom_symbols.md`
- TrustZone / TF-M feasibility: why BL616's E907 (RISC-V, M/U + PMP only, no ARM-style TrustZone-M) can't run the AmebaD-style TF-M secure world — vendor assessment (from-scratch, Zephyr, only 618DG evaluated feasible; PMP entries exhausted), the AmebaD Cortex-M33 contrast, and the project takeaway (no secure world on BL616; HACC/PKA crypto is NS-direct so needs no TZ; key protection falls back to eFuse): `doc/bouffalo_trustzone.md`

### Bouffalo Lab BL618DG (dual-core AP + NP)

**Two incompatible silicon flavours — pick the toolchain by `CPU_MODEL`, not by chip name:**

| CPU_MODEL | Core | `-march` | ABI | Toolchain | phyrf lib |
|---|---|---|---|---|---|
| `a0` (A-series) | T-Head e907 | `rv32imafcpzpsfoperand_xtheade` | `ilp32f` | `toolchain_gcc_t-head_linux` | `libbl618dg_phyrf.a` |
| **`b0` (mass production)** | **Nuclei-900** | `rv32imafdc_zicsr_zifencei` | `ilp32d` | **`toolchain_gcc_zephyr_linux`** | `libbl618dg_phyrf_b0.a` |

- **Bouffalo confirmed volume shipments are the Nuclei-900 (`b0`) part** — build `CPU_MODEL=b0`
  (also `project.build`'s default). Verified booting on HW 2026-08-01.
- Toolchain: `scripts/install_zephyr_riscv_toolchain.sh` fetches Zephyr SDK `riscv64-zephyr-elf`
  (gitignored, 1.1 GB — release tarballs only, so it can't be a submodule like the T-Head one).
  The bundled T-Head GCC 10.2 **cannot** build b0 (rejects the `zifencei` extension). The SDK's
  b0 branch doesn't pass `-mtune`, so Nuclei's `-mtune=nuclei-900-series` is not needed.
- **Both cores must be built.** `tools/make/dualcore.mk`: build NP first, then AP with
  `CONFIG_DUALCORE_NP_IMAGE=<np.bin>` which **embeds the NP image into the AP image**. A lone AP
  image does not boot. Reference build:
  `cd platform/bouffalo_sdk/examples/bl618dg_dualcore && make CPU_MODEL=b0 && make whole CPU_MODEL=b0`
  → `build/whole_flash_data.bin` (boot2 @0x0, partition @0xE000, FW0 @0x10000).
- Console: **AP UART0 TX = GPIO11 @ 2000000** 8N1.
- Flash (this bench): **COM30 = data (bootrom ISP + console), COM31 = control** (DTR→BOOT,
  RTS→reset, both active-low: `DtrEnable=$false` ⇒ BOOT pin HIGH ⇒ ISP). Hold COM31 open for the
  whole run. Script: `D:\MCU_Tools\bflb-cklink\bl618dg_flash_com30.ps1`. Cheap liveness probe:
  in ISP, write 64×`0x55` at 2 M on COM30 → bootrom answers `OK`.
- **B0-only policy.** Bouffalo ships B0 exclusively from here on, so everything vendored into
  `component/` is B0-only: `bl618dg_std` carries just the `mn` startup variant (`mx` = A-series,
  `lp` = LP core are not vendored), `bl618dg_phyrf` carries only `libbl618dg_phyrf_b0.a`, and
  `bl618dg_board_config` hard-sets `CPU_MODEL_B0`. Linking an A-series artifact into a B0 image
  fails outright ("can't link single-float modules with double-float modules").
- In-project support (mirrors bl616/bl616cl, dual-core modelled on AmebaD):
  - Toolchains: `cmake/bl618dg-{ap,np}-toolchain.cmake` → `bouffalo-toolchain-common.cmake` with
    `BOUFFALO_RISCV_TRIPLE=riscv64-zephyr-elf`. `BOUFFALO_CPU_ID` (ap|np) is what the component
    layer branches on.
  - Components: `component/bouffalo_sdk/bl618dg_{std,phyrf,board_config}` + bl618dg arms in
    `bflb_lhal` (incl. the Nuclei **n900** arch headers — it must NOT build the T-Head
    `rv_hart`/`rv_pmp` or link the ilp32f `csi_dsp` lib) and `bflb_sys`.
  - The two cores build **different** `bl618dg_std` source sets, matching the SDK: AP → ROM API
    (`romapi_e907` + `romapi_patch`, despite the `_e907` name it is the B0 chip's ROM table);
    NP → the full register drivers (SDK force-sets `CONFIG_ROMAPI=n` for NP).
  - Both cores expand the same `bl618dg_common.ld.in`, which splits one physical RAM via
    `CPU_AP`/`CPU_NP` + `CONFIG_{RAM,WRAM}_FOR_AP_SIZE` — so those sizes must agree across cores.
- Ethernet: **two** RMII MACs (`EMAC_V2_0/1`, a Synopsys DWMAC-style IP driven by
  `bflb_emac_v2.c` — not the bl616 `bflb_emac.c`), 10/100 only, full pin map + PHY support +
  porting traps from bl616: `doc/bl618dg_emac.md`
- Pin mux / pad conflicts (per-peripheral pin lists, the periodic mux rules, ★boot straps —
  GPIO33 = BOOT, UART ISP = GPIO11/12, USB ISP = GPIO40/41 — and the three-way fight between
  external flash / SD host / SDIO device over the same two pad groups): `doc/bl618dg_pinmux.md`

### Realtek Ameba D (RTL8721D / RTL8710C)
- SDK: `platform/ameba-rtos-d` (driver-only, dual-core: KM0 LP + KM4 HP)
- Build: `cd project/amebad && make` — each image (`app_km0`, `app_km4`, `bootloader_km0`, `bootloader_km0_2nd`, `bootloader_km4`) is its own **standalone CMake + Ninja project** (`make <image>` builds just one), each configured into its own `build/<image>` tree with the per-core toolchain (`cmake/amebad-km{0,4}.cmake`, which FORCE-sets `AMEBAD_CHIP`) and pulling the libs via `add_subdirectory(component)`; plain `make` (= `make image`) builds the four needed images and assembles via `component/amebad/image_tool/build_image_all.py`. There is no root `/CMakeLists.txt`; the cross-platform libs live in `component/` (self-sufficient — it adds the universal `*_config` itself). See the `project_amebad_standalone_projects` memory. Options: `make NIMBLE=ON` (NimBLE host + wolfSSL SM crypto), `make HCI=ON` (bare HCI/DTM test image), `make QUIC=ON` (ngtcp2 QUIC client self-test + full wolfSSL TLS 1.3/QUIC; builds & flashes, but on-HW boot is blocked by the KM4 XIP hang — see `doc/amebad_quic_test.md`), `make TFM=ON` (TF-M secure world / SPE; `make STORAGE=ON` adds PSA ITS+PS encrypted secure storage — see `doc/amebad_tfm_storage.md`), `make MINIMAL=ON` (WiFi-connect-only image; strips the iperf / LittleFS+ROM-mbedTLS KAT / TF-M HW-crypto bench diagnostics — pairs with `TFM=ON` for a TF-M + WiFi-connect build), `make AWS=ON` (lean AWS IoT telemetry client — aws_telemetry.c, same AWS/AWSOTA split as bl602: coreMQTT + switchable TLS, publishes telemetry by default; key defaults to a LittleFS DER file like bl602 — `STORAGE=ON` opts into the TF-M ITS backend; Thing name = BT MAC; see the `project_amebad_aws_iot` memory), `make AWSOTA=ON` (full AWS IoT agent image, implies AWS=ON: coreMQTT-Agent + FreeRTOS OTA v3.3.0 — aws_iot_demo.c; TFM agent builds drop the telemetry task, `AWS_NO_TELEMETRY`), `make CARES=ON` (c-ares v1.34.6 async DNS resolver replacing the FreeRTOS+TCP resolver — connected-UDP shim + `select()` loop over `component/syscall_config`; default OFF, bake on `AWS=ON CARES=ON`; verified resolve→MQTT→CloudWatch on HW; **needs a dirty 64 KB→2 KB UDP-read-buffer patch in the c-ares submodule or it wedges the TLSF heap** — see the `project_amebad_cares_dns` memory), coreSNTP UTC time-sync is **default-on in every non-QUIC image** (no flag): syncs UTC over UDP → backs newlib `_gettimeofday`/`time()` via `component/syscall_config/time_base.c` → wall-clock log timestamps, started async from the network-up hook; real UTC verified on HW; see the `reference_amebad_newlib_time` memory. `make distclean` (drop build trees, e.g. to switch generator). Needs the ASDK ARM toolchain at `toolchain/` (gitignored, install separately).
- Output: `build/amebad/Image_All.bin` (KM0+KM4 boot + image2; flash @ 0x08000000 via `project/amebad/flash.ps1`)
- Build system / image pipeline: `doc/build_system.md`, `doc/amebad_image_build.md`; the layout-sensitive XIP boot-hang gotcha: `doc/amebad_xip_layout_boot_hang.md`; TF-M secure storage (ITS+PS) + the flash/memory layout: `doc/amebad_tfm_storage.md`; the chip's key/cert inventory (RSIP/RDP/HUK/SBOOT eFuse map + manufacturer key-handling): `doc/amebad_keys.md`

### Realtek Ameba Dplus (RTL8711Dx) and Green 2 (RTL8711F)
- SDK: `platform/ameba-rtos` (Python + CMake build via `ameba.py`)
- Environment: `source project/ameba/env.sh` (or `env.bat` on Windows)
- Build: `cd project/ameba && make`
- Outputs: `project/ameba/build_RTL8711Dx/Image_All.bin` and `project/ameba/build_RTL8711F/Image_All.bin`
- SoC selection: `project/ameba/soc_info.json` and `project/ameba/prj.conf`
- Config: `project/ameba/CMakeLists.txt`, `project/ameba/Kconfig`

## Source Code Architecture

`src/main.cpp` is the only application source file (~2300 lines). It conditionally includes platform headers and implements:

- **Platform init** (`board_init()`, `hal_wifi_start_firmware_task()`, etc.)
- **Wi-Fi event handling** — scan, connect, got-IP, disconnect, AP start/stop, STA add/del
- **TCP/IP stack init** — `tcpip_init()` with a callback that kicks off Wi-Fi events
- **Bluetooth LE** — advertising, GATT service setup, GAP configuration; uses Realtek RTK BT stack on Ameba platforms, Zephyr-based BT stack on Bouffalo platforms
- **EasyFlash** KV storage for NV config
- **FreeRTOS** scheduler

Preprocessor symbols that identify platforms:
- Bouffalo: `BL602`, `BL702`, `BL606P`, `BL808`, `BL616`, `BL616CL`
- Ameba D: `CONFIG_PLATFORM_8721D`, `CONFIG_PLATFORM_8710C`
- Ameba newer: `CONFIG_PLATFORM_AMEBALITE`, `CONFIG_PLATFORM_AMEBASMART`, `CONFIG_AMEBADPLUS`, `CONFIG_AMEBAGREEN2`

## BL602 Hardware / Debug Setup

- **JLink probe**: S/N `601023163`, JTAG mode, identifies as RISC-V (`-device RISC-V`; `device BL602` is rejected by JLink V9.38a). `JTAGConf 0 0` for `-noir`-style chain.
- **UART0 console**: COM64 on host @ **2 Mbps**, 8N1.
- **UART1 (test/secondary)**: GPIO4 TX / GPIO3 RX, COM16 on host @ 115200, 8N1.
- **Reset via nTRST** (no power cycle): JLink commander `ClrTRST; Sleep 100; SetTRST` — does not require/leave the CPU halted.
