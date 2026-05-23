---
name: amebad-imagetool-flash
description: Flash AmebaD (RTL8721D) firmware over UART using the Realtek MP_ImageTool_1_N tool from WSL. Drives the two-COM bring-up (COM10 for download-mode strap, COM25 for data) end to end via PowerShell — handles tool/chip-name changes, UNC-path gotchas, and the "tool exit code lies" trap.
argument-hint: [--boot-port COM10] [--flash-port COM25] [--baudrate 1500000] [--skip-mode-check]
allowed-tools: [Bash, Read, Edit]
---

# AmebaD ImageTool flash SOP

Drives `platform/ameba-rtos/tools/ameba/MP_ImageTool_1_N/Ameba_1-N_MP_ImageTool.exe` from WSL to flash `project/amebad/out/Image_All.bin` onto a Realtek RTL8721D over UART. Hardware wiring + bootstrap timing are documented in [doc/ameba_image_download.md](../../../home/zxf/at-project/doc/ameba_image_download.md).

Working flow verified 2026-05-20 on the at-project bench with COM10 (boot strap) + COM25 (data).

## Pre-flight

1. **Firmware exists**: `ls -la /home/zxf/at-project/project/amebad/out/Image_All.bin` — should be 4 MB.
2. **Tool path**: `ls /home/zxf/at-project/platform/ameba-rtos/tools/ameba/MP_ImageTool_1_N/Ameba_1-N_MP_ImageTool.exe`. (Old path `Ameba_1_N_MP_ImageTool_v2.5.21/` was renamed; see "Tool/chip-name history" below.)
3. **Both COM ports enumerated on Windows**:
   ```bash
   powershell.exe -NoProfile -Command "[System.IO.Ports.SerialPort]::GetPortNames()"
   ```
   Must include COM10 (boot strap) and COM25 (data). If they're different on this host, pass overrides.
4. **Driver script**: [project/amebad/flash.ps1](../../../home/zxf/at-project/project/amebad/flash.ps1) — this is the working PowerShell driver. Read it before changing flow.

## Run

```bash
PSPATH='\\wsl.localhost\Ubuntu-24.04\home\zxf\at-project\project\amebad\flash.ps1'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PSPATH"
```

Or with overrides:
```bash
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PSPATH" \
    -BootPort COM10 -FlashPort COM25 -Baudrate 1500000
```

Healthy output (in order):

```
[boot] open COM10 to drive CHIP_EN/LOG_TX ...
[boot] LOG_TX low
[boot] CHIP_EN high (power on into download mode)
[boot] release LOG_TX
[chk] sniff COM25 @ 115200 for 0x15 ...
[chk] got: 15 15 15 15 15 15 15 15 15 15 15 15 15 15 15 15
[flash] launching ImageTool ...
  ChipSelect    AmebaD
  BaudRate:       1500000
  ...
  1      COM25      SUCCESS
  Download Complete
[done] flash success
```

Tool also writes `<ToolDir>/log/YYYY_MM_DD_HHMMSS.txt` per run with `flash erase start` → `End Verify` → `WORKER: complete`.

## Bootstrap sequence (what flash.ps1 does on COM10)

Chip enters download mode when the bootstrap pin is LOW at power-on. The script:

1. RTS low → CHIP_EN low (chip off)
2. DTR low → bootstrap pin low
3. RTS high → chip powers on, latches bootstrap → enters download mode
4. DTR high → release bootstrap so subsequent UART traffic isn't pinned low

Sanity check: COM25 @ 115200 should stream `0x15` continuously after step 3.

Note: COM25's own RTS/DTR are also wired to CHIP_EN/bootstrap. To avoid the tool resetting the chip when it opens COM25, always pass `-g false` (DtrRtsConfig=false) — the script does this; don't change it.

## Tool/chip-name history (this trips people every time)

The tool was migrated and the chip selector changed semantics:

| Era | Tool dir | `-c` semantics |
|-----|----------|----------------|
| Old (≤ v2.5.21) | `Ameba_1_N_MP_ImageTool_v2.5.21/` | RTL chip ID — AmebaD = `rtl872xd` (NOT `rtl8721dx`, which silently picks the wrong floader and fails with `download flashloader error`) |
| Current | `MP_ImageTool_1_N/` | Device profile name — AmebaD = `AmebaD`; others in `Devices/Profiles/*.rdev` (`AmebaDplus`, `AmebaGreen2`, `AmebaLite`, `AmebaSmart`, `AmebaZ`) |

`flash.ps1` already defaults to `-c AmebaD` against the new path; only override if testing a different SoC.

## Critical: tool exit code lies

`Ameba_1-N_MP_ImageTool.exe` exits **0 even when flashing FAILS**. `flash.ps1` greps stdout for `FAIL` / `SUCCESS` — don't rely on `$LASTEXITCODE`. If you write a new wrapper, replicate this behavior or you'll think a failed flash succeeded.

## UNC / path gotchas when scripting around it

- `flash.ps1` lives in WSL and is invoked via `\\wsl.localhost\Ubuntu-24.04\...` UNC path. That's fine — but inside PowerShell over UNC:
  - `[System.IO.Path]::GetFullPath` and `Resolve-Path` reject `..` traversal past the share root. The script implements `Resolve-WslPath` manually to handle this. Don't replace it with Resolve-Path.
  - `$PSScriptRoot` may not expand inside `param()` defaults under `-File` invocation; the script defers default-value assignment to the body.
- Tool runs fine when CWD is the WSL UNC path; no need to stage to `/mnt/c`.

See [[reference-wsl-powershell-serial]] for general WSL ↔ PowerShell serial conventions.

## Common failures

- **`download flashloader error` in tool log**: wrong chip name. On old tool (`rtl8721dx` selected for AmebaD) or new tool (mismatched device). Re-check `-c` value matches table above.
- **`1      COM25      FAIL` immediately**: COM25's RTS toggled and reset the chip out of download mode. Make sure you're not opening COM25 between bootstrap and the tool launch. The script's `Test-DownloadMode` sniff was OK in practice because the chip was already locked into download mode by the time the sniff ran, but if you add extra port operations on COM25, re-do `Enter-DownloadMode` right before launching the tool.
- **0x15 not detected after bootstrap**: bootstrap pin didn't latch — usually weak USB-Serial DTR pull-down (see doc's COM23 note). Try a different USB-UART adapter on COM10. Per doc, COM23 specifically can't enter download mode on this bench — that's why we use COM10.
- **Tool says SUCCESS but board boots old firmware**: AmebaD's flash write protection. Different problem — see [[reference-amebad-flash-layout]] and the SR2 unlock note in [[ameba-openocd-flash]]. This skill only covers the UART download path.

## After flash

The script's `[post]` step pulses CHIP_EN low/high on COM10 to reset the chip out of download mode into the new firmware. No manual reset needed.

To watch the app come up on the LOG UART, sniff COM25 at 115200 (or whatever console baud the app uses).

## When NOT to use this skill

- **Not for amebadplus / amebagreen2 / amebasmart / amebalite over OpenOCD** — use [[ameba-openocd-flash]] instead (CMSIS-DAP + gdb flash, no UART bootstrap dance).
- **Not for bouffalo / bl602 / bl616** — different toolchain entirely.

Other Ameba targets via this same UART tool are theoretically supported (set `-Chip AmebaDplus`, etc.) but bootstrap wiring on a given bench may differ; verify the bootstrap-pin convention first.
