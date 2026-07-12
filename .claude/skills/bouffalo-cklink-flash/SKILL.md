---
name: bouffalo-cklink-flash
description: Flash Bouffalo (BL602/BL616/BL616CL) firmware from WSL, then reset + read the console log + optionally start a GDB server. BL616 flashes ONLY over UART (CKLink is debug-only for the E907 family) — separate COM23 control lines (DTR→BOOT active-low, RTS→reset) + COM22 data @2M, app console @2M (project firmware; a given image may differ), post-flash log captured open-reader-first-then-RTS-reset, driven by bl616_flash_com22.ps1. BL602 has two paths: (A) ★UART ISP flash via COM18 with a synced COM19-RTS reset — RELIABLE when CKLink halt fails; (B) CKLink via the patched bflb-iot-tool. Handles the CKLink "LOAD HELP BIN FAIL" halt-window / two-probe collision trap, the "flash succeeds but reset always fails" trap, the bl616 DTR/RTS polarity + SET-CLOCK-PLL-FAIL(=BOOT-not-held) traps, the two-UART/two-baud splits, and the C-Sky DebugServer + gdb-over-WSL caveats.
argument-hint: bl602|bl616|bl616cl [--whole] [--at-port COM18] [--log-port COM19] [--debug]
allowed-tools: [Bash, Read, Edit]
---

# Bouffalo CKLink flash + reset + log + debug (from WSL)

End-to-end HW flow for a Bouffalo dev board (verified on BL602 / bl602dk): flash, reboot,
capture the console boot log (COM19 @2M), and (optionally) bring up a GDB server. All tools
are Windows-side; drive them from WSL via `cmd.exe`/`powershell.exe`.

> **Which path?** On this bench CKLink (§CKLink below) intermittently dies at **`LOAD HELP
> BIN FAIL` / `Target is not open`** — the running app remuxes IO3=JTAG so the probe reads
> flash id but can't halt the CPU to load the eflash helper, and two VID_42BF probes collide
> on the RISC-V DM (software-disable is *blocked on this Windows edition*, so even unplugging
> leaves stale ghost entries). **When CKLink fails, use the UART ISP path below — it is the
> proven reliable method (verified 2026-07-11: `Verify success` SHA-match end to end).**

> **BL602 vs BL616 — completely different story.** The bl602/bl702 sections below (CKLink +
> UART-ISP-with-DTR/RTS-on-the-data-port) are **bl602-only**. **BL616 (and the whole 616/808/618
> E907 family) CANNOT be flashed over CKLink at all** — its eflash loader is ROM-resident and
> only speaks UART/USB (no JTAG data interface); CKLink is **debug-only** for bl616. Flash bl616
> over **UART with separate BOOT/reset control lines** — see the dedicated bl616 section next.
> (4-layer proof + working procedure: memory `reference_bl616_cklink_cannot_flash`.)

## ★ BL616 / bl616dk — UART ISP flash, separate COM23 control + COM22 data (VERIFIED)

**bl616 flashes ONLY over UART — CKLink is debug-only** (see the note above; do not waste time on
CKLink flashing for bl616). On this bench bl616dk uses **two ports** and a **separate control-line
adapter** (unlike bl602 where DTR/RTS live on the data port):

- **COM23** = an FTDI "USB Serial Port" whose **DTR → chip BOOT pin, RTS → chip reset**. These are
  **active-low at the pin**: `DtrEnable=$true` drives the pin **LOW**, `$false` drives it **HIGH**.
  So *BOOT pin HIGH = `DtrEnable=$false`*; *reset assert (low) = `RtsEnable=$true`*, *release
  (high) = `RtsEnable=$false`*.
- **COM22** = the CKLink-Lite probe's UART (composite `FACTORYAIOT_PROG_D5EA78` MI_01). It is the
  **bl616 bootrom UART0 (flash data)** AND the **app console** — but at two different bauds:
  **flash @ 2000000**, **app console @ 115200** (same port!).

**Enter ISP:** BOOT high + reset pulse → `DtrEnable=$false`; `RtsEnable=$true` (250 ms) → `$false`.
**Keep COM23 open** (holding BOOT high) through the whole flash. **Normal boot:** BOOT low + reset
pulse → `DtrEnable=$true`; `RtsEnable=$true`→`$false`.

**One-shot script** `D:\MCU_Tools\bflb-cklink\bl616_flash_com22.ps1` does enter-ISP → flash →
**open COM22 reader → RTS-reset → capture boot log** (all with COM23 held open):
```
# whole 4M image (--single --addr 0x0); -Fw defaults to fw_bl616\at_bl616_whole_flash_data.bin
powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\MCU_Tools\bflb-cklink\bl616_flash_com22.ps1 \
    -Baud 2000000 -Fw "C:\path\to\BL616_AT_4M.bin"          # add -LogBaud 115200 for a 115200-console image
# tool result + captured boot log:
powershell.exe -NoProfile -Command "Get-Content D:\MCU_Tools\bflb-cklink\bl616_flash_out.txt | Select -Last 20"
powershell.exe -NoProfile -Command "Get-Content D:\MCU_Tools\bflb-cklink\bl616_bootlog.txt"
```
**Post-flash log ORDER (baked into the script): open the COM22 reader FIRST, THEN pulse COM23 RTS
to reset** — otherwise the boot burst is gone before the reader is listening. Params: `-LogBaud`
(default 2000000 = the project's own bl616 AT console; a given image may differ — BL616_AT_4M.bin
was 115200), `-LogSeconds` (default 8). The log is saved to `bl616_bootlog.txt`.
Bare tool invocation it runs (official DevCube 1.8.7 tool — bl616 UART path uses the **ROM** eflash
loader, so no external loader bin is needed):
```
D:\MCU_Tools\BouffaloLabDevCube-v1.8.7\bflb_iot_tool.exe --interface uart --port COM22 \
  --baudrate 2000000 --chipname bl616 --single --firmware <win-path.bin> --addr 0x0
```
Success = `Load N/N {progress:100}` → `Sha caled by host == by dev` → `Verify success` →
`[All Success]` (verified 2026-07-12, two images SHA-matched end to end).

**Traps:**
- **Polarity is everything.** `DtrEnable=$true` for "BOOT high" FAILS (`shake hand fail`); it must be
  `$false`. The script's default (`-Invert` OFF) already maps pin-HIGH→`Enable=$false`; pass
  `-Invert` only if a differently-wired adapter needs the opposite.
- **`SET CLOCK PLL FAIL` = BOOT was not actually held high during reset.** Auto-download over a
  data-port-only reset (e.g. trying COM5/COM22 DTR/RTS) half-enters ISP and the ROM loader can't
  lock the high-speed PLL. With COM23 holding BOOT high it disappears — it is NOT a hardware/xtal
  fault on a healthy bl616dk+40M.
- **Reading the console:** the project's own bl616 AT firmware console is **COM22 @ 2000000**
  (`CONFIG_CONSOLE_UART_BAUDRATE`); a third-party image can differ (BL616_AT_4M.bin printed
  `invaild bind info` at **115200**, garbage at 2M). If 2M is garbage, try 115200 (`-LogBaud 115200`).
  **Open the reader BEFORE resetting**, and **keep COM23 OPEN** in run-state (`DtrEnable=$true`,
  `RtsEnable=$false`) while capturing — *closing* COM23 lets its DTR/RTS float and can hold the chip
  in reset (→ `LEN=0`, looks dead but isn't). Reboot with a COM23 RTS pulse (`$true`→`$false`).
- **`Access to COM23 denied`** = a prior PowerShell still holds the port; close/kill it (or ask the
  user to release) before re-running.
- **Two VID_42BF probes (bl602 + bl616) collide on the RISC-V DM** — irrelevant to bl616 *UART*
  flashing (COM22/COM23), but if you also debug bl616 over CKLink, disable the bl602 probe first
  (`FACTORYAIOT`, needs one UAC).

## ★ RELIABLE PATH — UART ISP flash of the whole image (COM18) + synced COM19 RTS reset

Flashes the complete 4 MB image over the boot-ROM UART ISP on **COM18**; a plain **COM19 RTS
reset** at the tool's prompt drops the chip into the ISP window (no BOOT button, no JTAG). This
sidesteps the CKLink halt/collision problem entirely.

**Wiring on this bench:** COM18 = UART0 (AT @115200 **and** the ISP-download UART); COM19 =
console/log @2000000 **and** its FTDI **RTS is wired to the module reset**. Logs are on COM19.

**Step 1 — build the whole image with the IAP boot2.** The whole-flash assembler
(`component/bouffalo_sdk/bflb_image_tool/bflb_elf_to_bin.py`) copies `board_configs/bl602dk/boot2_*.bin`
(glob — keep exactly ONE boot2 there). The UART-ISP whole-flash needs the **IAP** boot2, so
replace the release boot2 with `boot2_iap_release_5.8.1.bin`:
```
bcfg=component/bouffalo_sdk/bflb_image_tool/board_configs/bl602dk
git rm -q "$bcfg/boot2_bl602_release_v8.1.3.bin"
cp "/mnt/d/MCU_Tools/下载需要的其它分区文件/boot2_iap_release_5.8.1.bin" "$bcfg/boot2_iap_release_5.8.1.bin"
git add "$bcfg/boot2_iap_release_5.8.1.bin"
# force re-assemble (the whole_flash custom cmd DEPENDS on the elf, not boot2):
rm -f project/bouffalo/build_bl602/boot2_*.bin project/bouffalo/build_bl602/*whole_flash*.bin project/bouffalo/build_bl602/flash_prog_cfg.ini
cd project/bouffalo && make CHIP=bl602 BOARD=bl602dk    # embeds boot2_iap_release_5.8.1.bin (41512 B)
```

**Step 2 — stage + flash** with the one consolidated script `C:\Users\Public\bl602_uart_flash.ps1`
(also copied in this skill dir). It runs the **separate** UART tool (`bl602_flash_download_tool`,
NOT the cklink wrapper), and on the tool's `Please Press Reset Key!` it pulses COM19 RTS
**immediately** (no fixed wait) to drop the chip into ISP. Two modes:
```
# whole image (wipes LittleFS):
cp project/bouffalo/build_bl602/at_bl602_whole_flash_data.bin /mnt/d/MCU_Tools/bflb-cklink/fw_bl602/
powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\Users\Public\bl602_uart_flash.ps1 -Mode whole
```
The bare tool invocation it runs (`-Mode whole`):
```
.\bflb_iot_tool.exe --interface uart --port COM18 --single \
  --firmware D:\MCU_Tools\bflb-cklink\fw_bl602\at_bl602_whole_flash_data.bin --chipname bl602 --baudrate 921600
```
Synced reset = COM19 RTS **pull-down then pull-up** (`.RtsEnable=$true; sleep 200ms; =$false`;
DTR stays false), fired the instant `Please Press Reset` appears. Success = `Sha host == dev`
→ `Verify success` → `Program Finished` → `[All Success]`. The script does **not** kill the
proc early — the "Load NNN/404840" progress is the eflash loader; program+verify runs after
it (~17 s total).

**Step 3 — auto boot + log (unified).** On `[All Success]` the script **resets the module
again and records the boot log** to `C:\Users\Public\bl602_bootlog.txt` (COM19 @2M, until
`network UP`/`GOT_IP` or 20 s). **Read that file to analyze the boot — no separate capture
needed:**
```
powershell.exe -NoProfile -Command "Get-Content C:\Users\Public\bl602_bootlog.txt"
```
Expect `[WSVC] ... found/cached -> connect` → `network UP DHCP IP …` (or `[WSVC] no WIFI_SSID
… staying offline` if unprovisioned).

**★ whole-flash (`--single`) WIPES the LittleFS/KV partition** (@0x300000) → cached WiFi
creds are gone; the board boots to `[WSVC] no WIFI_SSID/PASSWORD provisioned - staying
offline`. Re-provision over COM19: `setenv WIFI_SSID <ssid>` / `setenv WIFI_PASSWORD <pw>`
then reset (writes to the KV in LittleFS) — OR bake it in up-front (next).

### ★ Config injection — pre-provision via [[lfs-image-preconfig]]
Instead of a wipe-then-`setenv`, bake WiFi creds / TZ / NTP / AWS cert+CA / custom files INTO
the whole-flash so the board comes up already provisioned. Generate the LittleFS image with
`gen_lfs.sh` (pass the same flags straight through), `dd` it into the whole-flash at `0x300000`
(seek=768×4 KB), then flash `-Mode whole`:
```
make CHIP=bl602 BOARD=bl602dk               # builds at_bl602_whole_flash_data.bin (iap boot2)
.claude/skills/lfs-image-preconfig/gen_lfs.sh --out /tmp/lfs_pre.bin \
    --wifi-ssid ZengOTA --wifi-password 123456789 --tz CST-8 \
    --ntp-server pool.ntp.org --endpoint a1xxx-ats.iot.ap-southeast-1.amazonaws.com \
    --aws-cert build/aws/cert.pem --ca build/aws/AmazonRootCA1.pem
cp project/bouffalo/build_bl602/at_bl602_whole_flash_data.bin /tmp/whole_pre.bin
dd if=/tmp/lfs_pre.bin of=/tmp/whole_pre.bin bs=4096 seek=768 conv=notrunc status=none
cp /tmp/whole_pre.bin /mnt/d/MCU_Tools/bflb-cklink/fw_bl602/at_bl602_whole_flash_data.bin
powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\Users\Public\bl602_uart_flash.ps1 -Mode whole
powershell.exe -NoProfile -Command "Get-Content C:\Users\Public\bl602_bootlog.txt"   # -> already connected
```
Verified 2026-07-11: boots straight to `[WSVC] ZengOTA found -> connect` → `network UP` with the
baked-in creds + `env` showing the pre-seeded vars. (bl602 has no TF-M, so an `--aws-key` also
pre-seeds; on an amebad TF-M image the key still needs a post-flash `aws key` — see the sub-skill.)
Alternative without a rebuild/embed: flash just the region — `--single --firmware lfs_pre.bin
--addr 0x300000` (the bl602 tool takes `--addr`); leaves the firmware untouched.

### ★ LFS-preserving firmware update — UART `--pt --firmware` (NO `--single`)
To update ONLY the firmware and **keep the provisioned LittleFS/KV** (WiFi creds, etc.), use
the SAME script with `-Mode ptfw` — it flashes partition + app at their offsets instead of the
whole 4 MB chip (drops `--single`, passes `--pt <toml> --firmware at_bl602.bin`):
```
cp project/bouffalo/build_bl602/at_bl602.bin /mnt/d/MCU_Tools/bflb-cklink/fw_bl602/
powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\Users\Public\bl602_uart_flash.ps1 -Mode ptfw
# then read the auto-captured boot log:
powershell.exe -NoProfile -Command "Get-Content C:\Users\Public\bl602_bootlog.txt"
```
Bare tool invocation (`-Mode ptfw`, same COM18 ISP + immediate COM19-RTS-on-prompt + auto boot-log):
```
.\bflb_iot_tool.exe --interface uart --port COM18 --pt <partition_cfg_4M.toml> \
  --firmware D:\MCU_Tools\bflb-cklink\fw_bl602\at_bl602.bin --chipname bl602 --baudrate 921600
```
It writes boot2/pt/firmware at 0/0xe000/0x10000 only (verify SHA is over the 688 KB firmware,
not 4 MB) → the LittleFS region is untouched. **Verified 2026-07-11**: `setenv WIFI_SSID
ZengOTA` + `WIFI_PASSWORD` → connects (`DHCP IP 192.168.4.234`); after a `-Mode ptfw` reflash
the board boots straight to `[WSVC] ZengOTA cached ch=11 -> direct connect (no scan)` →
`network UP` (bl602_bootlog.txt) — creds survived. A compiled-in `[PTFW-TEST]` marker also
confirmed the *new* image actually booted (not the old one). (`-Mode whole` would wipe LFS.)

---

## CKLink path (patched bflb-iot-tool) — use only if UART ISP is unavailable

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
