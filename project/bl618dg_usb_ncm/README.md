# BL618DG USB CDC-NCM / FreeRTOS+TCP validation

BL618DG B0 build entry for the shared application in `../bl616_usb_ncm` and
`component/cherryusb_config`. The pinned SDK supplies the USB core, OSAL and
BL618DG controller driver. This image runs on AP only with NP boot disabled;
Wi-Fi, Bluetooth, lwIP and PSRAM are not enabled. BL616 keeps its existing
default identity and build entry.

| Identity | BL616 | BL618DG |
| --- | --- | --- |
| Lab USB VID:PID | FFFF:6161 | FFFF:618D |
| Serial | BL616NCM0001 | BL618DGNCM0001 |
| Host MAC | 02:61:60:00:00:02 | 02:61:8D:00:00:02 |
| Device MAC | 02:61:60:00:00:01 | 02:61:8D:00:00:01 |
| Device IP | 192.168.78.1/24 | 192.168.79.1/24 |
| Host DHCP pool | .100-.103 | .100-.103 |

These separate identities/subnets allow both boards to be connected to the same
Windows host. DHCP provides no default gateway or DNS server. Both images send
the 100 Mbps nominal speed notification before link-up, preserving the Windows
speed-display fix. Nominal link speed is separate from measured payload rate.

## Build

```sh
make -C project/bl618dg_usb_ncm whole OUT=/tmp/bl618dg-usb-ncm-build -j6
```

The build selects `CHIP=bl618dg`, `BOARD=bl618dgdk`, `CPU_MODEL=b0`, `CPU_ID=ap`,
with 320 KiB each reserved for AP RAM and WRAM. No NP image is required by this
USB-only application. USB registers use the SDK's chip-specific `USB_BASE`
(`0x20087000` on BL618DG), rather than BL616's `0x20072000`.

## Program and verify

Bench wiring: **COM15** programming/console at 2 Mbaud, **COM23** BOOT/reset
control. Native USB must be connected separately to the Windows host. Do not use
the BL616 COM22 port or its D5EA78 CKLink probe for this board.

From Windows, at the repository root:

```powershell
.\project\bl618dg_pan\tools\flash.ps1 `
  -Fw \\wsl.localhost\Ubuntu\tmp\bl618dg-usb-ncm-build\whole_flash_data.bin
.\project\bl618dg_usb_ncm\tools\console.ps1 -Command ncm_status -Seconds 5
.\project\bl618dg_usb_ncm\tools\ncm_test.ps1 -Rounds 10
.\project\bl618dg_usb_ncm\tools\verify_link_speed.ps1 -Cycles 2
```

If Windows renumbers the serial ports, pass the confirmed `-DataPort` and
`-ControlPort` to the console/recovery scripts and `-DataPort` / `-CtrlPort`
to the shared flasher. Default paths and scripts for BL616 remain unchanged.
The HTTP test uses `192.168.79.1:8080`, generates 2 MiB `offset % 251` payloads,
verifies download SHA-256 and checks every upload byte on the board. Recovery
tests reset through COM23, then verify both nominal speed and complete transfers.
See [shared application details](../bl616_usb_ncm/README.md) for NTB16 framing,
DMA buffer ownership, protocol fixes and test limitations.

## Validation status

On 2026-11-02, the NCM image was programmed through COM36 at 500 kbaud
on a BL618DG identified by BootROM as revision a2, chip ID `5ac03c42e8b4`.
The board has 8 MiB external Flash (JEDEC bytes `20 40 17`, pin configuration
`0x24`). Host and device SHA-256 matched:
`780f9585501d2814ab47ed7a4fb5bfa9c6c731f67ee18587b4cd92638e86452a`.

This board uses COM36 for programming and console, with DTR connected to BOOT
and RTS to EN. Automatic reset/download entry has not been reliable; manual
reset while the serial reader was open captured successful startup. The package
register `0x2000C060` read `0x8060729F`, selecting UART0 TX GPIO11 / RX GPIO42,
2 Mbaud. The COM15/COM23 defaults above belong to the earlier bench setup;
do not apply those defaults to this board.

The running image reports USB initialization result 0 and an HTTP listener on
`192.168.79.1:8080`, but repeatedly reports link down and never reports USB
configured. After reconnecting native USB to Windows, the host reports
**Unknown USB Device (Device Descriptor Request Failed)** (`VID_0000:PID_0002`).
NCM enumeration, DHCP and throughput remain unverified. This is an enumeration
failure before NCM network-driver binding; the cause has not yet been established.
Do not reuse BL616 throughput numbers as BL618DG measurements. Flash verification
and runtime logs are retained under ignored `build/validation/com36/`.

For the next enumeration diagnostic, build a separate Full-Speed image with EP0
logging (neither diagnostic option is enabled in the default image):

```sh
make -C project/bl618dg_usb_ncm whole OUT="$PWD/project/bl618dg_usb_ncm/build_fs_debug" CONFIG_USB_NCM_FULL_SPEED_ONLY=y CONFIG_USB_NCM_ENUM_DEBUG=y -j6
```

This diagnostic image was programmed on 2026-11-02 and passed device SHA-256
verification (`f9fbf3be4a6db9f1ba904d713983fe12fe0b0760d7b8a4468f484c490557f316`).
Initial reset attempts did not confirm application startup. After the user
physically disconnected CH340 DTR/RTS and reset the board, UART captured the
Full-Speed diagnostic build (`23:12:34`) starting successfully. USB initialization
returned 0; `help` and `ncm_status` responded. NCM remained disconnected
(`generation=12`, RX/TX/invalid NTB counters all 0), without an EP0 SETUP log.

Windows root hub port 3 reported `UsbLowSpeed` through
`IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX`; the legacy connection-info
query independently returned `LowSpeed=1`. Both reported failed enumeration.
The diagnostic firmware is forced to Full-Speed, so the observed low-speed
attachment makes reversed DP/DM wiring a leading hypothesis. Physical wiring
or another electrical cause still requires verification; this is not a confirmed
wiring diagnosis. NCM enumeration, DHCP and transfer validation have not passed.

The startup stream was returned by the serial tool when capture was interrupted;
that interrupted process did not persist the raw startup file. The measured
runtime counters and host port results are recorded in
`build/validation/com36/metadata.json`.
