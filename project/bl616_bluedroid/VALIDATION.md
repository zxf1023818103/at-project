# BL616 port validation — 2026-11-01

## Outcome

The standalone BL616 SPP, PANU and Wi-Fi-only configurations compile and link.
BL618DG SPP also compiles after the shared changes. BL616 UART programming and
CKLink reset were exercised successfully, with a responsive application shell.
**SPP and PAN have not passed runtime validation on this device:** the controller
reports Classic Bluetooth unavailable. No throughput result is reported.

## Hardware evidence

- COM22 ROM handshake: BL616 A0, MAC B4:C2:E0:47:34:FF, Flash JEDEC ef4016, 4 MB.
- SDK dual-mode archive: `libbtblecontroller_bl616_ble1m2s1bredr1.a`.
- Trace: HCI Reset 0x0c03 → Command Complete status 0; Read Buffer Size 0x1005 → Command Status 0x01 (Unknown HCI Command). Before the guard, Bluedroid enable remained pending here.
- Disassembly of linked `btble_bt_isEnabled`: read word at 0x20056000, return `~(word >> 18) & 1`. CKLink read word **0x00040400**, making the vendor predicate false. This is evidence for this device; it does not prove that every BL616 SKU has identical capability.
- No eFuse writes or capability-bit bypass were performed.
- Final image detects the unavailable capability after controller initialization, reports `BL616 controller reports BR/EDR disabled; PAN/SPP unavailable`, and deletes its startup task. `ps` confirms a responsive shell with no stuck `pan_start` or Bluedroid host tasks.
- Final image SHA256: `5fa9e3c03b52c49aa88ffe73fc6be7954224847e97641457bbe73bc884b93fd8`.

## Programming and reset

Initial COM23-assisted attempts were stopped after the user clarified that
BL616 requires manual reset; later operations use only COM22 and CKLink.
One initial Windows FlashCommand copy lacked the BL616 Flash parameter file;
this was resolved by using the pinned SDK's complete Windows tool.
The final UART Flash transfer reported `Verification succeeded`.

The SDK tool's CKLink Flash read path was tested without an erase/write and
returned `Interface is not supported` / `IMG LOAD HANDSHAKE FAIL` (0050).
CKLink debugging was independently verified against the D5EA78 E907FP probe.
GDB read the temporary boot word at HBN 0x2000f108 and used the SDK-defined
POR bit at GLB 0x20000548. Clearing temporary boot selection and resetting
through CKLink reliably booted the application; setting it to UART ISP also
worked and avoids any dependency on COM23 wiring or a working application.

## Regression scope

- BL616 SPP: whole image build passed; final guard exercised on hardware.
- BL616 PANU: build passed; networking not exercised because Classic is unavailable.
- BL616 Wi-Fi-only: build passed; association/throughput not exercised in this task.
- BL618DG SPP: regression build passed; no BL618DG flashing in this task.
- Windows Flash/SPP script parsing and SPP C# compilation passed.
- Existing `project/bouffalo`, Ameba project sources, global component registration,
  shared TCP configuration and the original SDK were not changed.
- Compiler warnings from existing SDK/upstream sources remain.

Raw logs, manifest and images are in the ignored `build/validation/` directory.
The board is left on the final capability-checking SPP validation image.
