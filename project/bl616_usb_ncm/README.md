# BL616 CherryUSB CDC-NCM / FreeRTOS+TCP validation

Standalone USB Ethernet test image for the attached BL616, using the pinned
`platform/bouffalo_sdk_bl618dg` SDK. UART programming/console is **COM22 at
2 Mbaud**; the BL616 native USB port connects to the Windows host. COM23 belongs
to the BL618DG fixture and must not be used here.

The same application also has a [BL618DG build entry](../bl618dg_usb_ncm/README.md),
with distinct USB PID/MAC/subnet and explicit COM15/COM23 controls. The default
BL616 build and serial tools continue to select this board.

The SDK supplies CherryUSB core, FreeRTOS OSAL and the BL616 high-speed USB DMA
controller driver. `component/cherryusb_config` adds the missing upstream NCM
device class and a FreeRTOS+TCP network interface. Its upstream commit, license
and local changes are recorded in that component's `UPSTREAM` file. This image
does not enable Wi-Fi, Bluetooth or lwIP. Existing Ameba/Bouffalo applications
and their component registration are not changed by this standalone project.

## Build and program

```sh
make -C project/bl616_usb_ncm whole OUT=/tmp/bl616-usb-ncm-build -j6
```

From Windows, when a shell-capable firmware is already running on COM22:

```powershell
.\project\bl618dg_pan\tools\flash.ps1 -Chip bl616 -DataPort COM22 -SoftwareBoot `
  -Fw \\wsl.localhost\Ubuntu\tmp\bl616-usb-ncm-build\whole_flash_data.bin
```

For initial programming, enter UART ISP using BOOT/reset and use `-ManualBoot`
instead. The existing BL616 CKLink helper can also select ISP; on this USB image
that method did not reliably enter the UART loader, while `reboot uart` through
`-SoftwareBoot` succeeded. Always check the flasher's verification result.
The UART loader's reset did not consistently return this board to the app;
the verified normal-boot method is the existing D5EA78 CKLink probe and helper:

```sh
LD_LIBRARY_PATH=/tmp/bl616-gdb-libs \
  platform/toolchain_gcc_t-head_linux/bin/riscv64-unknown-elf-gdb -q -batch \
  -x project/bl616_bluedroid/tools/cklink_reset.gdb
```

That helper requires a debug server on localhost:1025 connected specifically to
`CKLink_Lite_Vendor-FactoryAIOT Prog D5EA78`. The library directory is a local
toolchain compatibility fixture. Manual normal reset is an alternative after
clearing the software ISP selection. No eFuse operation is used.

## USB and network behavior

- Lab VID/PID `FFFF:6161`, serial `BL616NCM0001`; these are test identifiers.
- Windows automatically binds its built-in `UsbNcm` driver. No custom INF was
  installed. Current interface description is `CherryUSB 026160000002`.
- Host MAC `02:61:60:00:00:02`; device MAC `02:61:60:00:00:01`.
- Device address `192.168.78.1/24`. The test DHCP server leases `.100` through
  `.103` for 120 seconds; the tested Windows host received `.103`.
- DHCP advertises no router or DNS options. The USB interface therefore adds
  no default route; existing wired/Wi-Fi Internet access is retained.
- HTTP on port 8080 exposes `/status`, `/file?bytes=2097152` and `/upload`.
  Each file byte is `offset % 251`. Downloads are SHA-256 verified on Windows,
  and the board validates each upload byte. Files are generated/checked in RAM;
  this fixture does not program an external target's flash or capture UART RX.
- Shell `ncm_status` prints USB link/generation, packet/drop/invalid-NTB counters
  and HTTP statistics. Shell `reboot` exercises USB re-enumeration.

The transport implements NTB16 with 2048-byte transfers and Ethernet frames up
to 1514 bytes. RX validates NTH/NDP boundaries and chained tables before copying
frames into FreeRTOS+TCP buffers in a worker. TX uses four DMA buffers and a
worker queue; buffers remain owned until completion or a USB generation change.
USB interrupt callbacks signal workers rather than allocating network buffers.
Zero-length RX completions rearm reception. Firmware byte counters wrap at 4 GiB.

Two bring-up fixes were needed in the upstream NCM glue: data interfaces must
advertise protocol 1 (NCM NTB) for Windows to find its data alternate, and media
presence is notified when alternate 1 activates, independently of a packet-filter
request. Otherwise Windows bound UsbNcm but reported Code 10, or started with
media disconnected. See the [Microsoft host implementation](https://github.com/microsoft/NCM-Driver-for-Windows/blob/release_21H2/host/device.cpp).

## Windows validation, 2026-11-01

```powershell
.\project\bl616_usb_ncm\tools\ncm_test.ps1 -Rounds 10
.\project\bl616_usb_ncm\tools\console.ps1 -Command ncm_status -Seconds 5
```

The Windows-native test bypasses HTTP proxies and verifies both directions.
Default rate threshold is 1.6 Mbps, corresponding to 200,000 bytes/s from a
2 Mbaud 8N1 UART before extra framing and operating margin.

| Test | Download payload rate | Upload payload rate | Result |
| --- | ---: | ---: | --- |
| Initial 10 pairs, 40 MiB | 30.191 Mbps | 14.279 Mbps | All lengths/hashes/content passed |
| Additional 3 pairs, 12 MiB | 30.079 Mbps | 10.682 Mbps | All passed |
| Re-enumeration cycle 1, 1 pair | 30.392 Mbps | 28.710 Mbps | Passed |
| Re-enumeration cycle 2, 1 pair | 30.362 Mbps | 28.898 Mbps | Passed |
| Re-enumeration cycle 3, 1 pair | 29.818 Mbps | 28.688 Mbps | Passed |

Each of the three cycles rebooted the board through COM22, allowed Windows to
re-enumerate and renew DHCP, then verified a new 2 MiB transfer in each direction.
All 64 MiB of complete transfers were verified. The initial upload range was
4.709-29.188 Mbps; rates include HTTP/connection overhead and are not raw USB bus
bandwidth. The bus enumerated at high speed (512-byte bulk endpoints).
Final board counters showed zero RX/TX drops, zero invalid NTBs and zero HTTP
failures; idle available heap returned to 387,604 bytes after the last cycle.

The initial image displayed nominal LinkSpeed as 0 bps despite a functioning
link. This was fixed by sending `CONNECTION_SPEED_CHANGE` before
`NETWORK_CONNECTION(up)`. A debugger read confirmed the original image had sent
both 100,000,000-bit/s rates and completed its 16-byte interrupt transfer. The
[Microsoft adapter implementation](https://github.com/microsoft/NCM-Driver-for-Windows/blob/release_21H2/adapter/adapter.cpp)
caches rates in `SetLinkSpeed`, then publishes the cached values in `SetLinkState`.
Sending link-up first published the initial zero rates. After reordering, Windows
reports both TransmitLinkSpeed and ReceiveLinkSpeed as 100,000,000 (100 Mbps).
This is the configured nominal Ethernet rate, not measured USB throughput.
Three fresh bidirectional 2 MiB pairs passed after this fix. Two further board
reboots both retained the 100 Mbps display and passed fresh bidirectional pairs,
for 20 MiB verified after the fix. The table above uses
measured application payload rates from the initial image. A Windows PnP restart attempt
was denied by Windows permissions, so it is not counted as a successful test;
recovery measurements above use actual board resets instead. Driver restart
without board reset, cable removal under active traffic, physical UART throughput,
iOS/iPadOS interoperability and long-duration qualification remain untested.

The board is left running this USB-NCM image. Raw logs, image SHA-256, host
enumeration and DHCP/route snapshots are in the ignored `build/validation/`.
The temporary CKLink debug server is stopped after testing.

To check rate publication and fresh payloads after two board reboots:

```powershell
.\project\bl616_usb_ncm\tools\verify_link_speed.ps1
```

Rate-fix flash verification, debugger baseline, Windows CIM snapshots, transfer
results and the updated image hash are retained in `build/validation/speed/`.
