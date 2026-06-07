---
name: amebad-openocd-debug
description: Attach a debugger to a Realtek AmebaD (RTL8721D) SoC over OpenOCD + RV CMSIS-DAP from WSL. Brings up both cores (KM4 Cortex-M33 @ AP2, KM0 Cortex-M23 @ AP1) with scripts/amebad_openocd.cfg and starts gdb servers (KM4 :3335, KM0 :3331). Debug only — NOT a flash path (use amebad-imagetool-flash to write firmware).
argument-hint: [--debug-only] [--km4-only] [--speed 4000] [--adapter daplink|jlink]
allowed-tools: [Bash, Read, Edit]
---

# AmebaD OpenOCD debug SOP

Attaches OpenOCD to AmebaD (RTL8721D) over the same xPack `openocd.exe` + RV CMSIS-DAP
host path the plus-family uses, driven from WSL. The AmebaD SDK (`platform/ameba-rtos-d`)
ships **only** J-Link / RLX-probe scripts and no OpenOCD cfg, so this skill uses the
hand-written, in-repo config `scripts/amebad_openocd.cfg`.

Verified 2026-06-07 on the at-project bench: both cores examine, halt/read/resume work.

> **Debug only.** Flashing AmebaD over gdb does **not** work — the running firmware
> locks the bottom of flash via SR2 (see `src/BoardFlashLock.cpp`), so the RAM
> flash-loader's writes silently no-op. To write firmware use [[amebad-imagetool-flash]]
> (UART ImageTool). This skill is for attach / halt / breakpoint / inspect only.

## What the config encodes

`scripts/amebad_openocd.cfg` (read it before changing flow). The CoreSight AP map comes
straight from the SDK J-Link scripts
(`platform/ameba-rtos-d/project/realtek_amebaD_va0_example/GCC-RELEASE/project_hp/jlink_script/AP{1,2}_KM4.JLinkScript`):

| AP | Type | Use |
|----|------|-----|
| AP0 | APB-AP | CTRL_AP / authentication |
| AP1 | AHB-AP | **KM0** — Cortex-M23 ("Real-M200"), gdb `:3331` |
| AP2 | AHB-AP | **KM4** — Cortex-M33 ("Real-M300"), gdb `:3335` |
| AP3 | APB-AP | SWD password compare (unneeded on an unlocked chip) |

This is the **same AP layout as amebadplus**, so the cfg mirrors the SDK's
`amebadplus_openocd.cfg` idiom (`transport select swd` → `swj_newdap` → `dap create` →
two `cortex_m` targets at `-ap-num 2` / `-ap-num 1`), retargeted to AmebaD core types.
M33 has no VECTRESET, so reset is `cortex_m reset_config sysresetreq`. There's a 3-fails-
in-30s `examine-fail` watchdog that `shutdown`s OpenOCD if the probe loses the DUT.

## Pre-flight

Same host setup as [[ameba-openocd-flash]]:

1. **OpenOCD**: `/mnt/c/Users/zxf10/AppData/Roaming/xPacks/@xpack-dev-tools/openocd/0.12.0-7.1/.content/bin/openocd.exe` exists.
2. **Probe enumerated**:
   ```bash
   powershell.exe -NoProfile -Command "Get-PnpDevice -PresentOnly | Where-Object { \$_.FriendlyName -match 'CMSIS|DAP|J-Link' } | Select-Object FriendlyName,Status"
   ```
   should list `RV CMSIS-DAP` (Status OK).
3. **gdb (often absent — prefer the no-gdb path below)**: `gdb-multiarch` is NOT installed on
   this host, and the SDK's `arm-none-eabi-gdb` can't run (Ubuntu 24.04 lacks `libncurses.so.5`;
   symlinking `.so.6→.so.5` FAILS — the old gdb needs versioned `NCURSES_5.x` symbols ncurses6
   doesn't provide). Installing needs sudo. **So drive OpenOCD directly via telnet `4444`** (see
   "No-gdb workflow" below) — `mdw`/`reg`/breakpoints cover almost everything without gdb.
4. **Config present**: `ls /home/zxf10/at-project/scripts/amebad_openocd.cfg`.

OpenOCD is the Windows `openocd.exe`, so pass the cfg by **UNC path**:
`\\wsl.localhost\Ubuntu\home\zxf10\at-project\scripts\amebad_openocd.cfg`.

## Connection check only (no gdb)

```bash
OCD='/mnt/c/Users/zxf10/AppData/Roaming/xPacks/@xpack-dev-tools/openocd/0.12.0-7.1/.content/bin/openocd.exe'
CFG='\\wsl.localhost\Ubuntu\home\zxf10\at-project\scripts\amebad_openocd.cfg'
timeout 40 "$OCD" -c "set ADAPTER_TYPE daplink" -c "set ADAPTER_SPEED 4000" -f "$CFG" -c "init; exit"
```

Healthy log (verified values):
```
SWD DPIDR 0x6ba02477
[ameba.km4] Real-M300 (KM4) r1p0 processor detected
[ameba.km4] Examination succeed
[ameba.km0] Real-M200 (KM0) r1p0 processor detected
[ameba.km0] Examination succeed
```

Quick functional probe (halt KM4, read state, resume):
```bash
timeout 40 "$OCD" -c "set ADAPTER_TYPE daplink" -c "set ADAPTER_SPEED 4000" -f "$CFG" \
  -c init -c "targets ameba.km4" -c halt -c "reg pc" -c "reg sp" -c "mdw 0xe000ed00" -c resume -c exit
```
Running firmware lands KM4 PC in XIP flash (`0x0e00xxxx`), SP in SRAM (`0x1002xxxx`),
CPUID@`0xE000ED00`=`0x721FD220` (implementer 0x72 = Realtek).

## Start a debug session (gdb servers up)

### Terminal A — OpenOCD
```bash
OCD='/mnt/c/Users/zxf10/AppData/Roaming/xPacks/@xpack-dev-tools/openocd/0.12.0-7.1/.content/bin/openocd.exe'
CFG='\\wsl.localhost\Ubuntu\home\zxf10\at-project\scripts\amebad_openocd.cfg'
"$OCD" -c "set ADAPTER_TYPE daplink" -c "set ADAPTER_SPEED 4000" -f "$CFG" -c init
```
Wait for `Listening on port 3335 for gdb connections` (KM4) and `3331` (KM0).

### Terminal B — no-gdb, drive via telnet 4444 (preferred; gdb-multiarch absent)

Leave Terminal A's `openocd ... -c init` running (it listens on telnet `4444`). From WSL,
`localhostForwarding` lets `nc` reach the Windows openocd:
```bash
printf 'targets ameba.km4\nhalt\nreg pc\nmdw 0x10013d0c\nresume\nexit\n' \
  | nc -q1 127.0.0.1 4444 | tr -d '\r\000'
```
Gotchas: each new telnet connection's default target may be KM0 → prefix `targets ameba.km4`.
Telnet lines have a leading NUL → strip with `tr -d '\r\000'` before grepping.
Resolve a PC to a symbol: `arm-none-eabi-addr2line -f -e <elf> <pc>` (toolchain `nm`/`addr2line`
under `toolchain/asdk-*/linux/newlib/bin/`; `/usr/bin/nm` returns nothing for ARM objects).
Symbol-bearing ELFs: `build/km4/component/amebad/app_km4/target_img2_km4` (5400+ syms; the
`*pure*.axf` is stripped) and the KM0 equivalent under `build/km0/...`.

**Boot-vs-hang discriminator (reliable):** read `uxCurrentNumberOfTasks` (addr via `nm`): a
small value (~14) = scheduler up = BOOTED; garbage (e.g. `0x6ef5934b`) = `.bss` never zeroed =
HANG. More trustworthy than PC (PendSV/idle/fault-patch make single PC samples noisy).
SecureFault: `SFSR`@`0xE000EDE4` (AUVIOL=0x08), `SFAR`@`0xE000EDE8`.

**`reset halt` lands in the ROM reset-handshake spin (driver-only).** SYSRESETREQ drops KM4
into `BOOT_ROM_ResetVsr` @ `0x10104468`, spinning on `0x480003F8` bit25 which driver-only never
sets — so a bp at `app_start` never hits. To proceed to firmware/app_start UNDER the debugger,
satisfy it once: `mww 0x480003F8 0x02000201`, then `resume`. (See [[reference-amebad-chplan-reboot-handshake]].)

**To debug a fresh boot, don't use openocd `reset run`** (it also lands in the ROM spin). Let
`flash.ps1`'s `[post]` CHIP_EN pulse boot the firmware, then just `init` + `halt` (attach to the
already-running image) — give it ~6-8 s after flash before halting so it's past `.bss`/sync.

If gdb-multiarch IS available, the gdb path still works: `gdb-multiarch -ex 'set architecture
armv8-m.main' -ex 'target extended-remote :3335' <elf>`; `monitor halt/reset halt/mdw/wait_halt`.

## Options

- **`--km4-only`** → add `-c "set ENABLE_KM0 0"`. Use when KM0 is wedged in tickless
  deep sleep and its DP is unreachable (`Failed to read memory at 0xe000ed00` on
  `[ameba.km0]`); KM4 still examines fine. Mirrors the plus-family "KM0 DP wedge".
- **`--speed N`** → `set ADAPTER_SPEED N` (kHz). 4000 verified stable on this RV
  CMSIS-DAP probe; higher risks USB I/O errors (see [[ameba-openocd-flash]] speed ceiling).
- **`--adapter jlink`** → `set ADAPTER_TYPE jlink` (cfg sources `interface/jlink.cfg`
  instead of `cmsis-dap.cfg`); default is `daplink`.

## After the session

OpenOCD must release the CMSIS-DAP before the next run grabs it: `printf 'shutdown\n' | nc -q1
127.0.0.1 4444`, or Ctrl-C Terminal A. Note **`reset run` does NOT re-run firmware on driver-only**
(SYSRESETREQ → the `BOOT_ROM_ResetVsr` spin again); to actually boot the firmware again, power-
cycle or pulse CHIP_EN (re-run `flash.ps1`'s post-reset). `shutdown` is all you need to free the probe.

**Stray `openocd.exe` pile-up (bites every multi-run session):** `timeout NN "$OCD" ...` kills the
WSL-side launcher on timeout but leaves the **Windows** `openocd.exe` orphaned, holding the probe →
the next run hangs / `interface already in use`. Kill stray ones before each run:
```bash
powershell.exe -NoProfile -Command "Get-Process -Name openocd -EA SilentlyContinue | Stop-Process -Force"
pkill -9 -f openocd.exe   # WSL side
```
A clean `init`+`shutdown` connection check confirms the probe is free.

## Common failures

- **`Error connecting DP: cannot read IDR` / examine fails**: SWD can't reach the DUT —
  unpowered, loose SWD cable, or board held in reset. The probe is USB-self-powered so it
  stays online while the chip goes dark.
- **`[ameba.km0]` examine fails but KM4 is fine**: KM0 in deep sleep → use `--km4-only`.
- **`interface already in use` / next run hangs**: an orphaned Windows `openocd.exe` (usually
  from a `timeout`-killed run) still owns the probe — kill it (see "After the session").
- **examine spins then OpenOCD exits**: the cfg's `examine-fail` watchdog tripped (3 fails
  in 30s) — power-cycle the DUT / re-seat the probe and retry.
- **`libncurses.so.5: cannot open shared object file`**: SDK's `arm-none-eabi-gdb` can't run and
  there's no `gdb-multiarch` here (the `.so.6→.so.5` symlink doesn't satisfy its versioned
  symbols) → use the telnet `4444` no-gdb path instead.
- **`cortex_m vector_catch all` doesn't stop on the fault**: it does NOT catch the M33
  SecureFault here — let it run free and read SFSR/SFAR after it hangs, or HW-breakpoint
  `app_start` and single-step. (OpenOCD single-step / register reads over XIP are also flaky;
  trust HW-breakpoint hits + post-hang SFSR/SFAR + the single-step PC stride.)

## When NOT to use this skill

- **Flashing AmebaD** → [[amebad-imagetool-flash]] (UART; gdb flash is blocked by SR2 lock).
- **amebadplus / amebagreen2 / amebasmart / amebalite** → [[ameba-openocd-flash]]
  (different SDK, SoC-specific reg/flash params, and a working gdb flash path).
