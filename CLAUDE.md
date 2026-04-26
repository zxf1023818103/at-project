# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an AT command firmware project targeting multiple embedded IoT SoC platforms. All platforms share a single source file (`src/main.cpp`) that uses heavy `#ifdef` guards to conditionally compile platform-specific initialization, Wi-Fi, and Bluetooth LE setup code. Each platform has its own build project under `project/`.

## Supported Platforms and Build Commands

Platform SDKs are managed as git submodules under `platform/`. Run `git submodule update --init --recursive` after cloning.

### Bouffalo Lab BL616 / BL618
- SDK: `platform/bouffalo_sdk` (CMake + ninja via `project.build`)
- Toolchain: `platform/toolchain_gcc_t-head_linux` (RISC-V, added to PATH automatically)
- Build: `cd project/bl616 && make`
- Output: `project/bl616/build/build_out/whole_flash_data.bin`
- Config: `project/bl616/proj.conf` (CMake variables), `project/bl616/CMakeLists.txt`

### Bouffalo Lab BL602
- SDK: `platform/BL602_SDK` (Make-based)
- Build: `cd project/bl602 && make`
- Output: `project/bl602/build_out/whole_flash_data.bin`
- Config: `project/bl602/proj_config.mk`

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
- Bouffalo: `BL602`, `BL702`, `BL606P`, `BL808`, `BL616`
- Ameba D: `CONFIG_PLATFORM_8721D`, `CONFIG_PLATFORM_8710C`
- Ameba newer: `CONFIG_PLATFORM_AMEBALITE`, `CONFIG_PLATFORM_AMEBASMART`, `CONFIG_AMEBADPLUS`, `CONFIG_AMEBAGREEN2`

`src/bouffalo.mk` sets BL602-SDK component flags and per-chip `CPPFLAGS` macros for the BL602 Make build.
