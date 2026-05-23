---
name: reference-ameba-openocd-flash
description: "How to flash + debug Ameba Dplus / Green2 / D over OpenOCD + CMSIS-DAP from WSL using the SDK's gdb scripts"
metadata: 
  node_type: memory
  type: reference
  originSessionId: 42f3082b-8468-40e9-8522-e5e8ab5f779c
---

WSL2 + Windows-side `openocd.exe` (xPack 0.12) + `RV CMSIS-DAP` debug probe can do both debugger attach and firmware flashing on Realtek Ameba SoCs (verified on amebadplus / RTL8711Dx).

**Where the pieces live:**
- OpenOCD: `C:\Users\zxf10\AppData\Roaming\xPacks\@xpack-dev-tools\openocd\0.12.0-7.1\.content\bin\openocd.exe`
- OpenOCD cfg per SoC: `platform/ameba-rtos/tools/scripts/jlink_script/openocd/<soc>_openocd.cfg` (amebadplus / amebagreen2 / amebasmart / amebalite)
- SDK gdb wrapper (computes -ex set values, drives gdb): `platform/ameba-rtos/tools/scripts/gdb.py` — usage: `python3 gdb.py <soc-project-dir> flash`
- SDK gdb scripts: `platform/ameba-rtos/tools/scripts/gnu_script/{rtl_gdb_flash_write.txt,rtl_gdb_jtag_boot_com.txt}`
- SoC config table for ports / flash params: `platform/ameba-rtos/tools/scripts/gnu_script/ameba_gdb_config.json5`
- Flash loader binary + symbols (per-SoC, gdb.py auto-copies into gnu_script/): `platform/ameba-rtos/component/soc/<soc>/project/project_km4/gnu_utility/gdb_floader/{flash_loader_ram_1.bin,target_FPGA.axf}`
- Built firmware (amebadplus example): `project/ameba/build_RTL8711Dx/build/project_km4/image/{km4_boot_all.bin,km0_km4_app.bin}`

**Port reconciliation (load-bearing):**
- amebadplus `_openocd.cfg` defaults gdb-port to `58920` (km4) / `58921` (km0)
- `ameba_gdb_config.json5` expects `2335` (km4) / `2331` (km0)
- Bridge with `openocd ... -f <soc>_openocd.cfg -c "ameba.km4 configure -gdb-port 2335" -c "ameba.km0 configure -gdb-port 2331" -c "init"`

**Environment workarounds:**
- SDK toolchain `arm-none-eabi-gdb` needs `libncurses.so.5` / `libtinfo.so.5` — Ubuntu 24.04 ships only `.so.6` and `pip` package install requires `--break-system-packages` flag. Use `/usr/bin/gdb-multiarch` (15.1) as drop-in; it parses the SDK scripts fine.
- `gdb.py` needs `pip install --user --break-system-packages json5`
- WSL2 mirrored networking is on this machine — `127.0.0.1:<port>` from WSL reaches Windows-side `openocd.exe`, so `HOST_IP=""` in `rtl_gdb_jtag_boot_com.txt` resolves to localhost and just works
- amebadplus_openocd.json next to the cfg is a VS Code / IDE launch config (NOT an OpenOCD script)

**Gotcha: SDK gdb scripts used to use Realtek/J-Link `monitor` vocabulary** (`monitor reset 0` / `monitor waithalt 2000`) and OpenOCD rejects with `Protocol error with Rcmd`. The submodule branch `at` was patched (`2060ae4e4`) to use `monitor reset halt` / `monitor wait_halt 2000` — works for OpenOCD but no longer compatible with J-Link GDB Server. If both adapters need to coexist, gate via a `$OPENOCD` switch variable inside the script.

**Reference commit:** `at-project@999c114` bumps the submodule to the patched commit.

**Post-flash serial check (Ameba Dplus DAP-Link board):** the on-board CMSIS-DAP exposes a USB-CDC virtual COM port, but **it's wired to LOGUART (PB5/PB4), not the AT UART (PB31/PB30) per [doc/serial_config.md](doc/serial_config.md)**. To talk to the AT service you need a separate USB-UART adapter on PB31/PB30. On this machine the FTDI-based adapter enumerates as COM10 @115200 — verify after replug with the `Get-PnpDevice` discovery snippet in [[reference-wsl-powershell-serial]] (USB VID/PID stays stable, COM number may not). DAP-Link CDC port responds to `AT\r\n` with nothing; the right port echoes the command and returns `\r\nOK\r\n`.
