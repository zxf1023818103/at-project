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
- Flash: `cd project/bl602 && make -j flash`
- Debug: `cd project/bl602 && make start-gdb-server`

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

## BL602 Hardware / Debug Setup

- **JLink probe**: S/N `601023163`, JTAG mode, identifies as RISC-V (`-device RISC-V`; `device BL602` is rejected by JLink V9.38a). `JTAGConf 0 0` for `-noir`-style chain (matches `start-gdb-server` Make target).
- **UART0 console**: COM64 on host @ **2 Mbps**, 8N1.
- **UART1 (test/secondary)**: GPIO4 TX / GPIO3 RX, COM16 on host @ 115200, 8N1.
- **Reset via nTRST** (no power cycle): JLink commander `ClrTRST; Sleep 100; SetTRST` — does not require/leave the CPU halted.

## BL602 SDK Pitfalls (learned the hard way)

### ROM/RAM FreeRTOS StreamBuffer struct mismatch
`platform/BL602_SDK/components/platform/soc/bl602/bl602/evb/ld/flash_rom.ld` `PROVIDE`s many FreeRTOS symbols at fixed BL602 boot-ROM addresses (e.g. `xStreamBufferSend = 0x210167a8`, `xStreamBufferReceive = 0x210169ae`). The ROM was compiled against an older FreeRTOS whose `StreamBuffer_t` field layout differs from the SDK's current `freertos_riscv_ram` source. Mixing them — buffer created by RAM `xStreamBufferGenericCreate`, written by ROM `xStreamBufferSend`, drained by RAM `xStreamBufferReceiveFromISR` — yields field-offset-mismatched reads, garbage pointers, and a misaligned-load that secondary-faults inside the misaligned trap handler (`mcause=5` Load access fault, `mepc` inside `misaligned_load_trap`). Symptom: chip crashes/reboots in a tight loop the first time the StreamBuffer is used.

**Fix in `project/bl602/Makefile`**: wrap the FreeRTOS RAM library with `--whole-archive` so its strong symbols beat the ROM PROVIDEs:
```make
LDFLAGS := $(subst -lfreertos_riscv_ram,--whole-archive -lfreertos_riscv_ram --no-whole-archive,$(LDFLAGS))
```
Targeted alternatives (link only `stream_buffer.o` directly, or `--allow-multiple-definition`) snowball: `stream_buffer.o`'s undefined refs (`xTaskNotifyWait`, `vTaskSuspendAll`, `xTaskResumeAll`, `vTaskSetTimeOutState`, `xTaskCheckForTimeOut`, `xTaskNotifyStateClear`, `vEnvironmentCall`) are not in the ROM PROVIDE list, so they pull `tasks.o` from the archive, whose strong defs of `vTaskDelay`/`xTaskCreate`/etc. then beat the ROM PROVIDEs and produce a half-RAM-half-ROM FreeRTOS that diverges and runs off into the weeds (PC ends up in random non-code addresses). Don't go halfway — `--whole-archive` is the consistent choice.

### `bl_uart_init()` does not install the UART IRQ vector
`bl_uart_init()` in `components/platform/hosal/bl602_hal/bl_uart.c` masks all UART interrupts and does **not** call `bl_irq_register(UARTx_IRQn, UARTx_IRQHandler)`. Calling `bl_uart_int_tx_enable()` afterwards only sets the TX FIFO interrupt enable bit on the UART peripheral; the IRQ controller has no handler to dispatch to, so the TX interrupt is silently dropped, the consumer ISR never runs, and any task blocked on `xStreamBufferSend` waits forever (visible via JLink as the AppTask suspended in `xTaskNotifyWait`).

To use UART interrupts (TX or RX notify callbacks), call `bl_uart_int_enable(id)` once after `bl_uart_init()` — that registers `UARTx_IRQHandler`. If only TX is wanted, follow with `bl_uart_int_rx_disable(id)` / `bl_uart_int_tx_disable(id)` to clear the masks; xsputn-style code should manage TX enable on its own.

### `bl_uart_init()` first-call clock-divider switch corrupts in-flight UART bytes
`bl_uart_init()` guards a `static uint8_t uart_clk_init` and on first call runs `GLB_Set_UART_CLK(1, HBN_UART_CLK_160M, 3)`, which reprograms the **shared** UART clock divider for both UART0 and UART1. If UART0 (the console) has bytes in its TX FIFO/shifter at that moment, the bit clock mid-byte changes and the receiver decodes garbage for several bytes (typical symptom: a few characters in a `printf` banner come out as `??`). Do `bl_uart_init()` *before* any `printf` you care about, or otherwise drain the console first.
