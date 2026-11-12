# BL616 Bluedroid validation

BL616 build entry for the shared Bluedroid PANU/SPP validation in
`../bl618dg_pan`. Uses the isolated, pinned SDK at
`platform/bouffalo_sdk_bl618dg` and FreeRTOS+TCP; existing `project/bouffalo`
BL616 images and Ameba components are unchanged.

**The attached BL616 does not expose Classic Bluetooth.** Its controller
rejects HCI Read Buffer Size (0x1005) as Unknown HCI Command. The vendor
`btble_bt_isEnabled()` predicate also returns false for its capability word.
The application now reports this and exits the startup task instead of
blocking in Bluedroid enable. PAN/SPP traffic has therefore not been validated
on this board. A library named `bredr1` alone does not establish device capability.

## Build

```sh
make -C project/bl616_bluedroid whole SPP=ON
make -C project/bl616_bluedroid
make -C project/bl616_bluedroid WIFI_ONLY=ON
```

Use distinct `OUT=/tmp/...` directories for different configurations. The BL616
wrapper defaults to its own `build/`; direct `CHIP=bl616` builds in the shared
project default to `build_bl616/`. BL618DG keeps its existing default paths.
Enable `HCI_TRACE=ON` for command/event diagnostics.

BL616 uses the T-Head compiler, its own dual-mode controller archive, single-core
startup and chip partition/boot images. BL618DG-only CPU/NP settings, combo RF
selection, forced GPIO16 and manufacturing-header workaround are conditional.
BL616 coexistence and the BL618DG RF calibration diagnostics are rejected by the
Makefile until separately ported and validated.

## Bench programming and reset

Data and console: **COM22 at 2 Mbaud**. This board has no confirmed serial reset
control wiring; **do not use COM23**. Verified CKLink debug probe identity:
`CKLink_Lite_Vendor-FactoryAIOT Prog D5EA78`.

CKLink register access and reset work. The current SDK FlashCommand tool rejects
its BL616 CKLink Flash path with `Interface is not supported` / error 0050.
Thus the verified pipeline uses **CKLink for ISP entry and normal reset**, and
**COM22 for Flash transfer**. No eFuse programming is performed.

With the XuanTie DebugServer already connected to this BL616 on port 1025:

```sh
# Only for BL616 and a server connected to the intended board.
riscv64-unknown-elf-gdb -q -batch -x project/bl616_bluedroid/tools/cklink_isp.gdb
```

Then on Windows from the repository root:

```powershell
.\project\bl618dg_pan\tools\flash.ps1 -Chip bl616 -DataPort COM22 -ManualBoot `
  -Fw .\project\bl616_bluedroid\build\whole_flash_data.bin
```

The BL616 default tool is the pinned SDK's Windows FlashCommand executable,
which includes the required BL616 Flash parameters. After verification, open
a COM22 console reader, then reset through CKLink:

```sh
riscv64-unknown-elf-gdb -q -batch -x project/bl616_bluedroid/tools/cklink_reset.gdb
```

The helpers change temporary HBN boot selection and trigger BL616 POR. They
are not suitable for BL602, BL616CL or BL618DG. `reboot uart` also enters ISP
from a running shell, but FlashCommand `--reset` did not clear this board's
software boot selection; a CKLink reset was required to return to the app.
The Windows SPP runner now accepts `-DataPort`, `-ControlPort`, and
`-NoControlPort`; with the latter, `-Reset` uses the firmware shell reboot.
Only use it for a board whose Classic capability check passed, with its actual
Bluetooth address supplied through `-Address`.

See [VALIDATION.md](VALIDATION.md) for hardware evidence and limitations.
