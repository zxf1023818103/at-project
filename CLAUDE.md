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

### Realtek Ameba D (RTL8721D / RTL8710C)
- SDK: `platform/ameba-rtos-d` (Make, dual-core: KM0 LP + KM4 HP)
- Build: `cd project/amebad && make`
- Output: `project/amebad/out/Image_All.bin` (assembled from KM0/KM4 images)

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
