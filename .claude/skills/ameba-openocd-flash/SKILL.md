---
name: ameba-openocd-flash
description: Attach a debugger to / flash firmware onto a Realtek Ameba SoC (amebadplus / amebagreen2 / amebasmart / amebalite) over OpenOCD + CMSIS-DAP from WSL. Drives the SDK's gdb-based flash flow (rtl_gdb_flash_write.txt + flash_loader_ram_1.bin) end to end.
argument-hint: amebadplus|amebagreen2 [--debug-only] [--port-km4 N] [--port-km0 N]
allowed-tools: [Bash, Read, Edit]
---

# Ameba OpenOCD + GDB flash SOP

This skill assumes the at-project repo at `/home/zxf10/at-project` with `platform/ameba-rtos` submodule. Working flow verified on amebadplus / RTL8711Dx with RV CMSIS-DAP probe.

## Pre-flight (one-time)

1. **OpenOCD on Windows side**: `/mnt/c/Users/zxf10/AppData/Roaming/xPacks/@xpack-dev-tools/openocd/0.12.0-7.1/.content/bin/openocd.exe` exists.
2. **Probe enumerated**: `powershell.exe -NoProfile -Command "Get-PnpDevice | Where-Object { \$_.FriendlyName -match 'CMSIS|DAP|J-Link' } | Select FriendlyName"` should list `RV CMSIS-DAP` (or whatever probe is plugged).
3. **WSL networking**: confirm WSL2 mirrored mode is on so the SDK's `HOST_IP=""` resolves to localhost — `timeout 2 bash -c 'echo > /dev/tcp/127.0.0.1/<openocd-port>'` should succeed once openocd is up.
4. **gdb available**: `/usr/bin/gdb-multiarch --version` ≥ 15 works. SDK's `arm-none-eabi-gdb` won't run on Ubuntu 24.04 (`libncurses.so.5` missing) — use multiarch.
5. **json5**: `python3 -c "import json5"` — if missing, `pip install --user --break-system-packages json5`.
6. **Firmware built**: e.g. `project/ameba/build_RTL8711Dx/build/project_km4/image/km0_km4_app.bin` and `km4_boot_all.bin` present.

## Connection check only (no flash)

```
cd /home/zxf10/at-project/platform/ameba-rtos/tools/scripts/jlink_script/openocd
'/mnt/c/Users/zxf10/AppData/Roaming/xPacks/@xpack-dev-tools/openocd/0.12.0-7.1/.content/bin/openocd.exe' \
  -c "set ADAPTER_TYPE daplink" -c "set ADAPTER_SPEED 3000" \
  -f <soc>_openocd.cfg -c "init; exit"
```

Healthy log shows: `[ameba.km4] Examination succeed` and `[ameba.km0] Examination succeed`. If either fails 3 times in 30s the cfg's `ameba_examine_hook` will `shutdown` openocd — pull the probe / power-cycle the DUT and retry.

## Flash (gdb.py driven)

Two terminals.

### Terminal A — start OpenOCD with SDK-expected ports

```
cd /home/zxf10/at-project/platform/ameba-rtos/tools/scripts/jlink_script/openocd
'/mnt/c/Users/zxf10/AppData/Roaming/xPacks/@xpack-dev-tools/openocd/0.12.0-7.1/.content/bin/openocd.exe' \
  -c "set ADAPTER_TYPE daplink" -c "set ADAPTER_SPEED 3000" \
  -f <soc>_openocd.cfg \
  -c "ameba.km4 configure -gdb-port 2335" \
  -c "ameba.km0 configure -gdb-port 2331" \
  -c "init"
```

Wait for `Listening on port 2335 for gdb connections`.

### Terminal B — run gdb.py flash

The SDK's `gdb.py` defaults to picking the toolchain gdb from CMakeCache. That's broken on Ubuntu 24 — easier to skip `gdb.py` and copy its constructed command, then swap in `/usr/bin/gdb-multiarch`. Concretely:

```
cd /home/zxf10/at-project/project/ameba/build_RTL8711Dx/build/project_km4
/usr/bin/gdb-multiarch --batch \
  -ex 'set architecture armv8-m.main' \
  -ex 'set $SCRIPT_DIR="/home/zxf10/at-project/platform/ameba-rtos/tools/scripts/gnu_script"' \
  -ex 'set $GDB_PORT=2335' \
  -ex 'set $FlashFileSize='$(stat -c%s ./image/km0_km4_app.bin) \
  -ex 'set $XIPBootSize='$(stat -c%s ./image/km4_boot_all.bin) \
  -ex 'set $Img3FileSize=0' -ex 'set $DSPFlashSize=0' \
  -ex 'set $SYSTEM_CTRL_BASE=0x41008000' -ex 'set $REG_LSYS_BOOT_CFG=0x26c' \
  -ex 'set $PC=0x9000' -ex 'set $SP=0x30009ffc' -ex 'set $FLOADER_ADDR=0x3000a020' \
  -ex 'set $WR_CHECK_EN=0' -ex 'set $IMG2_ADDR=0x14000' -ex 'set $IMG3_ADDR=0x1e0000' \
  -ex 'set $TZ_SETUP_EN=0' \
  -ex 'set $BIN_IMG2_ALL="./image/km0_km4_app.bin"' \
  -ex 'set $BIN_IMG1_BOOT="./image/km4_boot_all.bin"' \
  -ex 'set $BIN_KM4_IMG3="./image/km4_image3_all.bin"' \
  -x /home/zxf10/at-project/platform/ameba-rtos/tools/scripts/gnu_script/rtl_gdb_flash_write.txt
```

Numbers above are for `amebadplus` from `ameba_gdb_config.json5`. For other SoCs look up the matching `REG_VARS` / `FLASH_PARAMS` block in that JSON5 file. Make sure `flash_loader_ram_1.bin` and `target_FPGA.axf` exist in `$SCRIPT_DIR` — `gdb.py` normally copies them from `component/soc/<soc>/project/project_<core>/gnu_utility/gdb_floader/`. If they aren't there, copy manually.

**SoC-specific bits worth remembering** (because the JSON5 is the only source and easy to miss):
- `amebagreen2` (RTL8711F): IMG_PROJECT is **`km4tz`** (not `km4`), bins are `amebagreen2_app.bin` / `amebagreen2_boot.bin` under `build_RTL8711F/build/project_km4tz/image/`. Cores in OpenOCD are `ameba.km4tz` (AP 2, default gdb 2335) and `ameba.km4ns` (AP 1, default gdb 2331). `SYSTEM_CTRL_BASE=0x4080A000`, `PC=0x20`, `SP=0x30005000`, `FLOADER_ADDR=0x30070020`, `IMG2_ADDR=0x14000`, `TZ_SETUP_EN=1`, `REG_LSYS_BOOT_ADDR_TZ=0x02F0`. Each SoC's `flash_loader_ram_1.bin` and `target_FPGA.axf` are different from amebadplus's — symptom of using the wrong axf is `Breakpoint at 0x3000a2ec` (Dplus's loader address) while the cfg loaded the bin to a totally different address. Keep backups under `$SCRIPT_DIR/*.dplus.{bin,axf}` if you flash multiple SoCs from the same checkout.

Healthy output: prints `[N]: write file 0x... to flash 0x...` for each 0x800-byte chunk, hits `Gdb_Floader_Program_End` breakpoint, then `[Inferior 1 (Remote target) detached]`.

### After flash — reset DUT and free the probe

`rtl_gdb_flash_write.txt` leaves the cores halted after detach. To start the new firmware and release the probe, use the OpenOCD telnet port (4444) from Terminal A's host:

```
echo -e "reset run\nshutdown" | nc -q 1 127.0.0.1 4444
```

`reset run` resets via `SYSRESETREQ` (the cfg auto-falls back from `VECTRESET` on Cortex-M33 — that's expected, not an error). `shutdown` exits OpenOCD so the next session can re-grab the CMSIS-DAP. Skipping `shutdown` will make subsequent `openocd.exe` invocations fail with "interface already in use" until the first process is killed.

## Flash write protection (critical!)

`src/BoardFlashLock.cpp` makes the running firmware lock the bottom 3 MB of
flash via SR1/SR2 (CMP=1, TB=0, BP=101) — that covers boot1, OTA1, OTA2,
mfg. **SDK's gdb flash script writes via the RAM flash_loader, which respects
the same SR bits, so writes to those regions silently no-op**. Symptom: gdb
session looks fine (chunks counted, `Gdb_Floader_Program_End` hits), but the
board boots the *previous* firmware on reset. Telltale: LFS reports an error
line that matches an older `r2.5x/lfs.c` even though your build is r2.80.

Workaround: unlock SR2 before flashing. Easiest with the SDK's
`platform/ameba-rtos/tools/ameba/ImageTool/AmebaImageTool.exe` (Realtek's
official tool — it sends Write Status commands over UART). Alternatively
attach gdb to the still-running locked firmware and `call FLASH_SetStatus
(0x31, 1, &sr2_unlocked)` where `sr2_unlocked = readStatus2() & ~0x40`
(clear CMP). Top 1 MB (LFS region, 0x300000+) is always unprotected, so
`lfs.bin` writes succeed even while bottom 3 MB is locked — verify firmware
flash with a fresh xxd dump of OTA1 / cmp against `km0_km4_app.bin` if in
doubt.

## Forcing Green2 into UART burn mode from GDB (when locked firmware breaks SYSRESETREQ)

Symptom — only seen on `amebagreen2` so far, but the trick generalises wherever the running firmware corrupts TZ/peripheral state in a way that survives `SYSRESETREQ`:

- After attach, KM4TZ examines fine and the cfg's `monitor reset halt` succeeds (`pc=0x20 msp=0x30001000` in Thread mode — looks clean).
- Then `c` from `PC=0x20` → ROM falls into HardFault very early: `CFSR=0x00000400` (UsageFault INVPC, i.e. bad EXC_RETURN), `HFSR=0x40000000` (forced), `pc=0x0000e6de`.
- Any subsequent `reset run` / `reset halt` puts the chip straight back into HardFault — flash_loader never runs and `Gdb_Floader_Program_End` is never hit. `monitor reset` is **not** enough to clear whatever the locked firmware left in TrustZone / always-on peripherals; OpenOCD only issues `SYSRESETREQ` (Cortex-M core reset), which doesn't wipe AON state.

Workaround — mimic the SDK's `cmd_reboot uartburn` (AT command in `component/at_cmd/monitor.c`) from OpenOCD telnet. Two things matter:

1. **Set the UARTBURN sentinel in `BKUP_REG0`.** It lives at `AON_BACKUP_REG_BASE + 0 = 0x4080A0E0`. The field is bits `[14:8]` (mask `0x7F00`), magic value `0x5A`. Boot count in `[27:24]` should be preserved.
2. **Trigger a SoC-level software reset, not `SYSRESETREQ`.** `System_Reset()` in `fwlib/ram_common/ameba_reset.c` does it by writing the LSYS SW-reset registers (a SoC-wide reset that clears the AON state SYSRESETREQ leaves alone):
   - `REG_LSYS_SW_RST_TRIG` (`0x4080A264`) ← `0x96969696` (unlock key)
   - `REG_LSYS_SW_RST_CTRL` (`0x4080A260`) ← OR in `LSYS_BIT_KM4TZ_SYS_RST = 1<<31` (or `KM4NS_SYS_RST = 1<<30` from NP)
   - `REG_LSYS_SW_RST_TRIG` ← `0x69696969` (trigger)

Concrete OpenOCD telnet sequence — paste into Terminal A's `nc 127.0.0.1 4444`. **The CTRL write must be read-modify-write — `LSYS_BIT_KM4TZ_SYS_RST` (bit 31) alone is not enough.** Bits 24/25 (`SRST{0,1}_GLB_RST_EN`, "reset all syson IP when this CPU resets") and bit 0 (`OTHERCPU_RST_EN`, "also reset the other core") sit in CTRL too; the boot loader programs them to `1`. Overwriting CTRL with a bare `0x80000000` clears those, so the reset hits only the KM4TZ core and leaves KM4NS + peripherals stale. End result: chip enters burn mode roughly half the time and AmebaFlash silently hangs at `Floader handshake timeout` (the floader was uploaded fine but a leftover peripheral or KM4NS state stops ROM from jumping). Read CTRL first, OR in the bits you need, write it back — matches what `System_Reset()` in `fwlib/ram_common/ameba_reset.c` does:

```
targets ameba.km4tz
halt
ameba.km4ns arp_halt
mdw 0x4080A260                       ; observe current CTRL (typical: 0x03ff0f71)
mww 0x4080A0E0 0x01005A00            ; BKUP_REG0 |= UARTBURN(0x5A<<8); preserve boot-cnt nibble
mww 0x4080A264 0x96969696            ; SYS_RESET_KEY (unlock CTRL)
mww 0x4080A260 0x83ff0f73            ; existing 0x03ff0f71 | KM4TZ_SYS_RST(bit31) | PERIALL_RST_EN(bit1)
mww 0x4080A264 0x69696969            ; SYS_RESET_TRIG (fire)
```

Healthy result, observable via a follow-up `halt; reg pc`:
- OpenOCD logs **both** `[ameba.km4tz] external reset detected` **and** `[ameba.km4ns] external reset detected`. If only the km4tz line appears, you've hit the bare-`0x80000000` trap above — re-do the CTRL write with the proper OR'd value.
- `pc ≈ 0x0000c900` in Thread mode (ROM's UART-burn receive loop), **not** the `0x0000e6de` HardFault.
- `mdw 0x4080A0E0` → upper byte preserved, low half cleared (ROM consumed the UARTBURN sentinel).

Once the chip is parked in UART burn mode, the locked firmware never runs again so BoardFlashLock no longer holds SR2 down. From here, the working path is **`platform/ameba-rtos/tools/ameba/Flash/dist/AmebaFlash.exe` over UART** — verified end-to-end 2026-05-22, 4 MB Image_All.bin written in ~42 s at 1.5 Mbps. **Don't use `tools/ameba/MP_ImageTool_1_N/` for Green2** — that tool errors `Chip name is not valid!`; its registered chip set tops out at AmebaSmart (`strings` confirms). The newer GUI `tools/ameba/ImageTool/AmebaImageTool.exe` only exposes `--genota` from CLI, so for headless flashing AmebaFlash is the only option.

Concrete invocation (run from WSL via PowerShell so Windows reads serial directly):

```bash
powershell.exe -NoProfile -Command "
\$exe  = '\\\\wsl.localhost\\Ubuntu-24.04\\home\\zxf\\at-project\\platform\\ameba-rtos\\tools\\ameba\\Flash\\dist\\AmebaFlash.exe'
\$prof = '\\\\wsl.localhost\\Ubuntu-24.04\\home\\zxf\\at-project\\platform\\ameba-rtos\\tools\\ameba\\Flash\\Devices\\Profiles\\RTL8711F_NOR.rdev'
\$img  = '\\\\wsl.localhost\\Ubuntu-24.04\\home\\zxf\\at-project\\project\\ameba\\build_RTL8711F\\Image_All.bin'
& \$exe --download --profile \$prof --start-address 0x08000000 --image \$img --port COM22 --baudrate 1500000 --log-level INFO --no-reset 2>&1
"
```

- **`--no-reset` is mandatory** here. AmebaFlash's default post-download "reset" toggles DTR/RTS, which on a board where those lines aren't wired to nRESET/strap (typical for AmebaGreen2 SWD bench) does nothing useful — but more importantly, if it ever did, it'd reboot into the locked firmware again. Just power-cycle / re-inject UARTBURN if you need to verify.
- **The SDK ships `AmebaFlash.exe` without its `Devices/` subdir staged next to the binary**. First run will fail with `Flashloader not exists: ...dist\\Devices\\Floaders\\floader_amebagreen2.bin`. Fix by **copying** (not symlinking — UNC `\\wsl.localhost\` can't traverse WSL symlinks) the parent `Devices/` next to `AmebaFlash.exe`:
  ```bash
  cp -r /home/zxf10/at-project/platform/ameba-rtos/tools/ameba/Flash/Devices \
        /home/zxf10/at-project/platform/ameba-rtos/tools/ameba/Flash/dist/Devices
  ```
- **0x15 sync byte is at 1.5 Mbps, not 115200** on Green2 (AmebaD emits at 115200 — different ROM). To sanity-check UART burn mode is live, sniff COM22 @ 1500000 8N1 — should see a continuous `0x15` stream. At 115200 you'll just see `0xFF`.
- **AmebaFlash needs a *fresh* UART burn mode entry per run.** Each failed attempt or even the success itself closes COM22, which momentarily toggles the host's DTR/RTS line. Even though those aren't wired to anything on this board, the chip leaves burn mode anyway (we've seen the 0x15 stream go silent after one round-trip). Always **re-inject UARTBURN via SWD right before each AmebaFlash invocation** — don't try to chain two runs against one injection.
- Healthy progress markers in the log: `Current supported flash size <=16MB` → device info (`FlashCapacity: 32Mb/4MB`, `WiFiMAC: ...`) → `Programming progress: 10/20/.../100%` → `Image_All.bin download done` → `Finished PASS`.

If the run dies at `Floader handshake timeout` immediately after `STX 23# done` / `Switch baudrate: 1500000 -> 1500000`, the chip wasn't actually in UART burn mode when AmebaFlash connected (or the floader file path was still wrong). The STX upload appears to succeed because ROM's command-handler ACKs the packets, but ROM never jumps to the uploaded floader, so the handshake sense (0x07) times out. Re-do the SWD UARTBURN injection sequence and confirm 0x15 emission before retrying.

All-GDB fallback (if UART path is unavailable for some reason): keep OpenOCD attached, **skip the script's `monitor reset halt` blocks**, manually load `flash_loader_ram_1.bin` to `FLOADER_ADDR`, set `PC=FLOADER_ADDR | 1` and `SP=0x30005000`, then `c`. Avoid any further `reset` until flashing is done — another reset re-enters the locked firmware (BKUP UARTBURN is already consumed) and you're back to the HardFault. (Not exercised end-to-end; the UART path above is what's actually verified.)

## SWD speed ceiling

Cfg comment says daplink supports up to `ADAPTER_SPEED 4000`. Empirically on
this RV CMSIS-DAP probe in WSL:
- **4000 kHz** — stable, used end to end
- **10000 kHz** — KM0 examine fails ("stalled AP operation"); KM4 can write
  flash for ~60 chunks then USB I/O Error
- **40000 kHz** — OpenOCD hangs before emitting any log; usbipd connection
  drops. Don't try this on this probe.

## Common failures

- **`Protocol error with Rcmd: 01`**: SDK gdb scripts still use J-Link `monitor reset 0` / `waithalt`. Patch (already merged in submodule branch `at`, commit `2060ae4e4`): `monitor reset 0` → `monitor reset halt`, `monitor waithalt 2000` → `monitor wait_halt 2000` in `rtl_gdb_jtag_boot_com.txt` and `rtl_gdb_flash_write.txt`. **If it still fires after the patch, see "KM0 DP wedge" below — that's the more common cause now.**
- **KM0 DP wedge / `Failed to read memory at 0xe000ed00` on `[ameba.km0]`**: the previously-running firmware put KM0 into deep sleep / tickless suspend, the M0+ debug bus is unreachable. KM4 still examines fine. The SDK gdb script's `monitor reset halt` resets all targets in the chain, hits the wedged KM0, returns `Protocol error with Rcmd: 01`, and aborts the whole flash. **Workaround:** start OpenOCD with KM0 disabled, only KM4 gdb port up:
  ```
  openocd ... -c "set ENABLE_KM0 0" -f <soc>_openocd.cfg -c "ameba.km4 configure -gdb-port 2335" -c "init"
  ```
  KM0 firmware is bundled inside `km0_km4_app.bin` and gets rewritten as part of flashing KM4's app slot, so disabling its debug target during flash is safe. After reset+shutdown the new firmware boots and KM0 wakes up normally.
- **`Error connecting DP: cannot read IDR`**: SWD itself can't reach the DUT. Probe is fine but the chip isn't responsive. Almost always a hardware issue: DUT unpowered, SWD cable loose, board held in reset. If a separate USB-UART (e.g. FTDI) also disappears at the same time, that's a strong "DUT board has no power" signal — the probe is USB-self-powered so it stays online while the chip goes dark.
- **gdb hangs on connect**: probe died or wrong gdb-port. Check Terminal A — if `Listening on port 2335` never appeared, the `configure -gdb-port` override didn't run before `init`.
- **`KM4 examine failed 3 times within 30 seconds`**: probe lost contact. CMSIS-DAP cable, DUT power, or wrong DAP-AP num for the SoC.
- **`libncurses.so.5: cannot open shared object file`**: using SDK's arm-none-eabi-gdb. Switch to `/usr/bin/gdb-multiarch`.

## IMG2_ADDR must match your Flash_Layout

`ameba_gdb_config.json5` gives default `IMG2_ADDR` per SoC (e.g. `0x14000` for amebadplus). That value is the flash offset where `km0_km4_app.bin` gets written **and** the address the new boot manifest gets pointed at. If you've changed [Flash_Layout[]](https://example.invalid) (e.g. shrunk `IMG_BOOT` to make room for OTA / LFS), the JSON5 default is stale. Override on the gdb invocation, e.g. `-ex 'set $IMG2_ADDR=0x10000'` to match `IMG_APP_OTA1`'s actual start. Symptom of mismatch: image gets written to wrong offset, boot ROM points to wrong place, device boots into the old firmware (or fails entirely).

## Reset / monitor commands cheat sheet (OpenOCD vocab)

| Need | OpenOCD `monitor ...` |
|---|---|
| Reset and let run | `monitor reset run` |
| Reset and halt at vector | `monitor reset halt` |
| Halt now | `monitor halt` |
| Wait for halt | `monitor wait_halt <ms>` |
| Sleep | `monitor sleep <ms>` |
| Raw memory read | `monitor mdw <addr>` |
