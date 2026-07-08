---
name: bouffalo-cklink-flash
description: Flash Bouffalo (BL602/BL616/BL616CL) firmware over CKLink from WSL, then reset + read the console log + optionally start a GDB server. Drives the patched bflb-iot-tool (auto-download, no BOOT button) via PowerShell/cmd.exe, handles the "flash succeeds but reset always fails" trap (use AT+RST to reboot), the two-UART/two-baud console split (AT COM@115200 vs log COM@2M), and the C-Sky DebugServer + gdb-over-WSL caveats.
argument-hint: bl602|bl616|bl616cl [--whole] [--at-port COM18] [--log-port COM19] [--debug]
allowed-tools: [Bash, Read, Edit]
---

# Bouffalo CKLink flash + reset + log + debug (from WSL)

End-to-end HW flow for a Bouffalo dev board (verified on BL602 / bl602dk): flash over
CKLink, reboot with `AT+RST`, capture the console boot log, and (optionally) bring up a
GDB server. All tools are Windows-side; drive them from WSL via `cmd.exe`/`powershell.exe`.

## Local tool locations (this machine)
- CKLink flasher: `D:\MCU_Tools\bflb-cklink\bflb-cklink.cmd` (patched 32-bit bflb-iot-tool
  1.10.0; forces empty CKLink serial so it connects by VID/PID `42bf|b210` "FactoryAIOT" —
  auto-download, no BOOT button). Its own README is in that dir.
- GDB server: `D:\C-Sky\DebugServer\bin\DebugServerConsole.exe` (XuanTie DebugServer).
- gdb (WSL): `platform/toolchain_gcc_t-head_linux/bin/riscv64-unknown-elf-gdb` (Xuantie).

## Build artifacts (per chip, in `project/bouffalo/build_<chip>/`)
- `at_<chip>.bin` — the app image (pass as `--firmware`).
- `boot2_<chip>_release_*.bin` — boot2 (`--boot2`).
- `at_<chip>_whole_flash_data.bin` — the complete 4 MB flash image (for `--single --addr 0x0`).
- Partition toml: `platform/bouffalo_sdk/bsp/board/<board>/config/partition_cfg_<N>M.toml`
  (bl602dk = `partition_cfg_4M.toml`).

The tool wants **Windows paths**, so stage the pieces into a Windows dir first
(`cp ... /mnt/d/MCU_Tools/bflb-cklink/fw_<chip>/`) and pass `D:\...` paths.

## 1. Connection self-test (confirm the probe)
```
cd /mnt/d/MCU_Tools/bflb-cklink
cmd.exe /c "bflb-cklink.cmd --chipname BL602 --interface cklink"
```
Look for `flash id` / `verification succeeded` / `[All Successful]`.

## 2. Flash
Component flash (tool assembles boot2 + pt + app):
```
cmd.exe /c "bflb-cklink.cmd --chipname BL602 --interface cklink ^
  --boot2 D:\MCU_Tools\bflb-cklink\fw_bl602\boot2_bl602_release_v8.1.3.bin ^
  --pt    D:\MCU_Tools\bflb-cklink\fw_bl602\partition_cfg_4M.toml ^
  --firmware D:\MCU_Tools\bflb-cklink\fw_bl602\at_bl602.bin"
```
Or the whole image raw at 0x0 (most faithful — rules out assembly mismatch):
```
cmd.exe /c "bflb-cklink.cmd --chipname BL602 --interface cklink --single ^
  --firmware D:\MCU_Tools\bflb-cklink\fw_bl602\at_bl602_whole_flash_data.bin --addr 0x0"
```
Success = `sha256 caled by host == caled by dev` + `[All Successful]`.

## ★ Two CKLink probes connected (bl602 + bl616): isolated no-UAC flash
When BOTH a bl602 probe and a bl616 probe are plugged in they share VID/PID `42bf|b210`, so
the RISC-V Debug Module transactions get **confused** → `ABSTRACTCS.cmderr 3` → `halt_cpu`
fails → `EFLASH LOADER HANDSHAKE FAIL` (flash never starts). **Serial selection can't fix it**:
the bl602 iSerial `FACTORYAIOT` is a *prefix* of the bl616 iSerial `FACTORYAIOT_PROG_D5EA78`,
so an empty/short `BFLB_CKLINK_SN` matches both. The only reliable fix is to **software-disable
the bl616 probe** for the duration of the bl602 flash. Probe identities (verify with
`Get-CimInstance Win32_PnPEntity | ? {$_.DeviceID -match 'VID_42BF'}`):
- bl602 probe = `USB\VID_42BF&PID_B210\FACTORYAIOT`
- bl616 probe = `USB\VID_42BF&PID_B210\FACTORYAIOT_PROG_D5EA78`

`Disable-PnpDevice` needs admin. To avoid a UAC prompt **per flash**, a pre-registered
**elevated scheduled task** does the whole dance; triggering it costs **no UAC** (only the
one-time task registration prompted once). Three files live in `D:\MCU_Tools\bflb-cklink\`:

- **`flash_bl602_dispatch.ps1`** (the task action, runs elevated): `Disable-PnpDevice` bl616 →
  run the bflb command staged in `flash_cmd.txt` → `Enable-PnpDevice` bl616 → **COM19 RTS reset**
  (boots the halted chip, see Trap 1). Logs to `isolated_flash.log` (UTF-16).
- **`flash_cmd.txt`** — the bflb command to run. Default = a **firmware component flash**
  (`--boot2 … --pt … --firmware at_bl602.bin`) which writes boot2+pt+firmware only and
  **PRESERVES the LittleFS/PSM partition @0x300000** (seeded AWS creds / KV). To flash something
  else (e.g. `--single --firmware lfs.bin --addr 0x300000` to reseed creds, or a whole-flash),
  overwrite this file — the task needs no change.
- **`create_flash_task.ps1`** — one-time setup: `Register-ScheduledTask -TaskName FlashBL602
  -RunLevel Highest` for the current user, action = run `flash_bl602_dispatch.ps1`.

Setup (ONE UAC, once ever):
```
powershell.exe -NoProfile -Command "Start-Process powershell -Verb RunAs -Wait -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File','D:\MCU_Tools\bflb-cklink\create_flash_task.ps1'"
```
Flash (NO UAC, every time): stage the new `at_bl602.bin` into `fw_bl602\`, then
```
cp build_bl602/at_bl602.bin /mnt/d/MCU_Tools/bflb-cklink/fw_bl602/at_bl602.bin
powershell.exe -NoProfile -Command "schtasks /run /tn FlashBL602"
```
Poll `isolated_flash.log` for `re-enabled bl616` / `RTS reset pulsed` = done (~35 s: disable →
~30 s flash → re-enable → reset). The task registration is flagged by Claude's auto-mode
classifier as "persistence" — it needs **explicit user authorization** ("装计划任务") before you
create it; do NOT register it unprompted. Without the task, fall back to a one-time elevated run
of the dispatch (`Start-Process -Verb RunAs … -File …\flash_bl602_dispatch.ps1` = one UAC per
flash, no persistence) or physically unplug the bl616 probe (no UAC).

## ★ Trap 1: flash succeeds but the chip stays HALTED
Every `bflb-cklink` run ends with a `CKLinkException: Target is not open` traceback
(`clear_boot_status → halt_cpu`) AFTER `[All Successful]`. The **flash is fine**; the
**post-flash reset fails**, so the CPU is left halted and does NOT run the app. Also:
- gdb `monitor reset` is a **no-op** (doesn't reset the SoC to its boot vector).
- The board has **no UART auto-reset** — toggling COM DTR/RTS does nothing.

**To actually run the app, use `AT+RST`** (see §3) or a physical power-cycle. Since the
board is powered by the CH340 console USB, fully unplugging that cable = a real power cycle.

**Third option — a pure-register soft reset over CKLink/gdb** (works even when the CPU is
halted and does NOT depend on firmware running an AT parser). `bl_sys_reset_por` →
`GLB_SW_POR_Reset()` (`drivers/soc/bl602/std/src/bl602_glb.c`) just pulses three bits in
**`GLB_SWRST_CFG2`** = `GLB_BASE(0x40000000) + 0x18` = **`0x40000018`**: clear then set
`PWRON_RST`(bit0) | `CPU_RESET`(bit1) | `SYS_RESET`(bit2) = mask `0x7`. Since POR is a
hardware action, a JTAG bus write triggers it regardless of core state. A single `set {int}`
memory write survives the WSL↔Windows bridge (unlike `continue`/`monitor reset`):
```
LD_LIBRARY_PATH=<ncurses-shim> riscv64-unknown-elf-gdb -q -batch \
  -ex "set remotetimeout 15" -ex "target remote 127.0.0.1:1025" \
  -ex "set \$v = *(unsigned int *)0x40000018" \
  -ex "set {unsigned int}0x40000018 = (\$v & 0xfffffff8)" \
  -ex "set {unsigned int}0x40000018 = (\$v | 0x7)"
```
Needs the DebugServer up (§4). The chip resets → gdb detaches → boot log streams on the
console port (§3). Verified on BL602: writing `0x40000018` rebooted straight into the app.

## ★ Trap 2: TWO UARTs at DIFFERENT bauds
- **AT command port** (bl602dk pins IO16/IO7) — often the **CH340**, **115200** baud.
  `AT` → `OK`; `AT+RST` reboots (the AT reset command, `src/AtCommandParser.rl` `+RST`).
- **Console/log port** (pins IO3/IO4) — **2000000** baud (`CONFIG_CONSOLE_UART_BAUDRATE`).
  This is where `printf` goes: RF-cal dump, `[SM-KAT] ... PASS`, `[NimBLE] ...`.
Boot log is one-shot at reset; an idle AT port emits nothing → don't mistake "0 bytes" for
"not booting". Identify ports with `[System.IO.Ports.SerialPort]::GetPortNames()` +
`Get-CimInstance Win32_PnPEntity` (CH340 vs generic USB-serial).

## 3. Remote reset + capture the boot log (no hands on the board)
Reboot via `AT+RST` on the AT port and read the console in the SAME PowerShell session so
the read window catches the boot burst:
```
powershell.exe -NoProfile -Command "
\$at=New-Object System.IO.Ports.SerialPort('COM18',115200,'None',8,'One');
\$log=New-Object System.IO.Ports.SerialPort('COM19',2000000,'None',8,'One');
\$at.Open(); \$log.Open(); \$at.DtrEnable=\$false; \$at.RtsEnable=\$false;
Start-Sleep -Milliseconds 200; \$at.DiscardInBuffer(); \$log.DiscardInBuffer();
\$at.Write('AT+RST' + [char]13 + [char]10);
\$sb=New-Object System.Text.StringBuilder; \$t=(Get-Date).AddSeconds(9);
while ((Get-Date) -lt \$t){ try{\$c=\$log.ReadExisting(); if(\$c){[void]\$sb.Append(\$c)}}catch{} Start-Sleep -Milliseconds 30 }
\$log.Close(); \$at.Close(); Write-Output \$sb.ToString()
"
```
Expected tail: `[SM-KAT] mbedTLS SM crypto: ALL PASS` → `[NimBLE] advertising as 'bflb-nimble'`.

### PowerShell-serial gotchas
- Write CRLF as `'AT' + [char]13 + [char]10` — **do NOT** use bash-backtick `` `r`n `` (the
  shell eats it → a garbled command → the board answers `ERROR` instead of `OK`).
- Don't live-echo raw bytes with `Write-Host $c` — binary chars crash it (exit 144); append
  to a StringBuilder and print once at the end.
- Right baud matters: reading the 2M console at 115200 returns **garbage** (`??hz??Q?...`),
  reading the idle AT port returns **0**. Both look like "nothing" but are different bugs.
- Sanity check the link: `AT` → `AT\r\nOK\r\n` at 115200 proves R/W works + board is alive.

## 4. GDB server (optional debug)
```
cd "/mnt/d/C-Sky/DebugServer/bin"
cmd.exe /c "DebugServerConsole.exe -port 1025 -prereset"   # run in background
```
It prints the E907 CPU info (RV32IMAFCX, 4 HW bkpt/wp) and listens on 1025. Connect gdb
(fix its missing lib first: `ln -s libncursesw.so.6 → .so.5` in a dir on `LD_LIBRARY_PATH`):
```
LD_LIBRARY_PATH=<shim> riscv64-unknown-elf-gdb -q project/bouffalo/build_bl602/at_bl602.elf \
  -ex "set remotetimeout 20" -ex "target remote 127.0.0.1:1025" -ex "print/x \$pc"
```
**★ Trap 3: gdb-over-WSL is unreliable for interactive debug.** Simple reads (`print $pc`,
memory reads) work over `127.0.0.1:1025` (WSL2 mirrored networking; the `172.x`/`192.168.x`
vEthernet IPs the server prints are slower). But `continue` / `monitor reset` corrupt over
the WSL↔Windows bridge → "Error packets ... connection lost" / all-stop-mode timeout. For a
real stepping/backtrace session, **run gdb natively on Windows** against `localhost:1025`.

## Verify (BL602 NimBLE)
1. Flash (§2) → `AT+RST` (§3) → console shows `[SM-KAT] ... ALL PASS` and
   `[NimBLE] advertising as 'bflb-nimble'`.
2. Scan/connect from a phone (nRF Connect) or nRF52 DK — it finds `bflb-nimble` and connects.
3. Optional: write the sample GATT chr → console prints `characteristic written: 0x..`.

See the `project_bouffalo_nimble_bringup` memory for the on-HW milestone + these traps.
