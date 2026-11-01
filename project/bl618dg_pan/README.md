# BL618DG PANU validation application

Latest SDK RF/coexistence integration is available with `COEX=ON`.
Use `COEX=ON COEX_RF=combo` for the application's Wi-Fi/PAN traffic test;
see [combo measurements and limitations](COMBO_VALIDATION.md). This mode has
transferred concurrent TCP traffic, but has not passed stability qualification.
Standalone PLL calibration still stalls on this board, including after cold
boot; `COEX=OFF` remains the default. See [standalone diagnosis and original
vendor demo comparison](COEX_VALIDATION.md).
`RF_DIAG=ON` builds the optional RF isolation shell in a separate output
directory; it is for calibration diagnosis and provides no PAN/IP service.

This B0 application uses Bluedroid Classic Bluetooth PANU to obtain network
access from a Windows/phone NAP. Networking uses FreeRTOS+TCP, including DHCP,
DNS and sockets. Sources live in `component/bluedroid_config`; this directory
contains the board application and validation tooling.

For the separate Wi-Fi-only test, use `WIFI_ONLY=ON`; see
[Wi-Fi-only build and test results](WIFI_VALIDATION.md).
The [single-radio throughput assessment](SINGLE_RADIO_VALIDATION.md) records
completed BT-only and Wi-Fi-only measurements, test images and TCP window limits.

## Build

Initialize the Bouffalo SDK, ESP-IDF and FreeRTOS+TCP submodules at the pinned
revisions recorded by the repository. Install Zephyr's RISC-V GCC 14.3 toolchain
under `platform/toolchain_gcc_zephyr_linux/bin` (the Makefile adds it to PATH).
The SDK's Linux `BLFlashCommand-ubuntu` must be executable and its dependencies
available. From the repository root:

```sh
make -C project/bl618dg_pan whole
```

The output is `build/whole_flash_data.bin` beneath this project. AP and NP are
built with matching RAM splits; the latest radio configuration runs AP only
and does not start NP. `HOST=OFF` builds the local HCI
capability smoke application instead; clean before changing HOST mode.

## Flash and console on Windows

Run these commands in Windows PowerShell from this project directory:

```powershell
.\tools\flash.ps1
.\tools\console.ps1 -Command 'pan_status'
```

Defaults are COM15 at 2,000,000 baud for programming/console and COM23 for
BOOT/reset. The flasher defaults to
`D:\MCU_Tools\bflb-cklink\bouffalo_flash_cube\BLFlashCommand.exe`; override `-Tool`
if needed. A 1.5-second reset pulse is used. Flash verification must succeed;
logs are saved to `build/validation`. Regional erase preserves pairing storage;
`-EraseAll` explicitly erases it. `console.ps1 -Reset` resets into normal boot.
Only one process may own these serial ports at a time.

The shared antenna switch is **GPIO16 high for Bluetooth**, low for 2.4 GHz
Wi-Fi. The default BT-only application sets it high before starting the controller.
The experimental `COEX=ON` build delegates RF initialization and SPDT control
to the latest SDK and adds Wi-Fi STA support through FreeRTOS+TCP. Both builds
preserve the existing `/KV` namespace for stored pairing records.

## Windows sharing with Hyper-V

On this host the Settings picker selected physical Ethernet while the actual
Internet connection was `vEthernet (External-Bridge)`. DHCP/DNS and local ping
worked, but TCP SYN packets never reached the public interface. Binding the
Bluetooth private adapter to the real routed public adapter through Windows
TetheringManager fixed HTTP access. The following helper queries that binding;
with `-Enable` it starts sharing using the specified public adapter:

```powershell
.\tools\windows_share.ps1 -PublicInterfaceAlias 'vEthernet (External-Bridge)'
# First turn Mobile hotspot off in Settings, then:
.\tools\windows_share.ps1 -PublicInterfaceAlias 'vEthernet (External-Bridge)' -Enable
```

During this bench test Windows repeatedly turned the hotspot off. Its
no-connections timeout was disabled with `-IdleTimeout Disable`; the tool's
query output reports the resulting state. Use `-IdleTimeout Enable` to restore
that timeout, or leave the default `Keep` to make no change.

Use your actual public adapter name. The helper explicitly selects Bluetooth
PAN as the private adapter. After changing this binding, reconnect the board.

## Connect

On Windows enable Mobile hotspot, choose Bluetooth sharing, and keep it enabled
throughout testing. Confirm it has not automatically switched off while the
board was disconnected. Pair `BL618DG-PAN` through Settings / Add device /
Bluetooth. Select **Show all devices** if the default list filters it out.
Pairing through Settings is required; a successful ACL authentication alone
does not prove Windows has retained a bond. The board persists its bond in
LittleFS. A whole-chip erase requires pairing again on both ends.

Use `tools/windows_radio.ps1` to read the host radio address and the board's
Windows authentication state; `-Scan` performs inquiry. Substitute the actual
NAP Bluetooth address below:

```text
pan_scan
pan_connect 20:0d:3d:9c:6d:81
pan_status
pan_http
pan_http www.baidu.com
pan_disconnect
```

`pan_http [hostname [port]]` performs an IPv4 HTTP GET and prints the first
response line, trying up to four resolved addresses with a ten-second connect
timeout per address. It is a diagnostic command, without TLS. A successful
connection reports `OPEN status=0` followed by `IP UP` with DHCP address,
gateway and DNS. `pan_status` reports the board address, bond count, link and
Ethernet packet/drop counters. After a reset, use `pan_connect` again; automatic
reconnect is not implemented.

For MTU validation on Windows, use the DHCP address printed by the board:

```powershell
ping -n 3 -w 6000 -l 1472 -f 192.168.138.246
```

## Validation and limits

On the attached B0 board, GPIO16 selection enabled Windows discovery and
Settings pairing. PAN/BNEP, DHCP, DNS, 32-byte ping and 1472-byte ICMP payload
(1500-byte IPv4 packet, 3/3 replies) have passed. After fixing the Windows
public-interface binding, `pan_http` returned `HTTP/1.1 200 OK`, including
fallback after the first resolved address timed out. Reset and reconnect with
persisted bonding also passed. Final connection: `192.168.138.19`, bond count
1, RX 108 / TX 20, RX/TX drops 0. Final MTU retest passed 3/3 replies,
38–139 ms (average 85 ms). Raw logs and packet captures are retained locally
in the ignored `build/validation` directory. The tested board address is B4:E8:42:3C:B5:2B and Windows radio is
20:0D:3D:9C:6D:81; these addresses are examples, not portable defaults.

This is a bring-up application. Long-duration stability, concurrent Wi-Fi/PAN traffic,
phone interoperability, BLE profiles and production pairing policy remain to
be validated. The controller occasionally emits an empty continuation ACL at
encryption setup; upstream logs an unknown-continuation warning, while PAN
connection and subsequent traffic succeed. This diagnostic remains to be
resolved before production qualification. See the component README for implementation boundaries.

## Standalone SPP throughput validation

Build with `make whole SPP=ON OUT=/tmp/bl618dg-spp-build`. This enables a
standard SDP Serial Port service (UUID 0x1101, RFCOMM channel 1) and leaves
Wi-Fi and PAN/TCP uninitialized. `SPP=ON` requires `COEX=OFF`.

After flashing with `tools/flash.ps1`, run `tools/spp_load.ps1 -Mode tx`
(board to Windows) or `-Mode rx` (Windows to board). Use `-Reset` for each
independent run. COM15 carries commands and logs, COM23 controls reset, and
the measured payload travels through a native Windows RFCOMM socket.
The socket resolves the standard SPP service through SDP; no virtual COM
port or configured UART baud rate limits the Bluetooth measurement.

`tools/summarize_spp.py` checks both endpoint counters and payload checks,
then writes a summary under `build/validation/spp`. See `SPP_VALIDATION.md`
for measurements and limitations. Generic SPP is not an iOS application
data channel; Apple External Accessory/MFi requirements need separate
evaluation.

## Isolation on amebad-aws-ota

The validation project uses `platform/bouffalo_sdk_bl618dg`, pinned to the
original SDK revision used by these tests. `platform/bouffalo_sdk` remains
at the branch's existing revision for BL602/BL616/BL616CL and other consumers.
The net_al patch is applied only to the dedicated BL618DG SDK by this
project's Makefile. Initialize the dedicated SDK and ESP-IDF submodules
before building on a fresh checkout.

`component/bluedroid_tcp_config` supplies the standalone TCP build setup and
reuses the existing pinned FreeRTOS+TCP source submodule. The shared
`component/freertos-plus-tcp_config` and global `component/CMakeLists.txt`
are unchanged; Ameba and existing Bouffalo images do not add these new
validation components. See `BRANCH_MIGRATION_VALIDATION.md` for checks.
