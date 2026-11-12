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

## Sustained GATT downlink fix (2026-11-02)

The previous ~31-second disconnect on both bearers was traced to ATT control
responses being dropped while bulk notifications congested L2CAP. iPhone's
Device Name Read By Type response was dropped on LE; a CCCD Write Response
failed on BR/EDR. Both were followed by about 30 seconds without the required
response and then disconnect.

The standalone GATT build now retains server responses in a four-PDU bounded
queue and flushes on the next BTU pass before sending more bulk data. The
congestion callback records state only, avoiding recursive L2CAP TX scheduling.
The patch hook is gated to `CONFIG_PAN_GATT_BR` / `CONFIG_PAN_GATT_LE`; it is not
enabled in PAN/SPP or other platform builds. Host queue fault injection and
post-change default PAN compilation passed.

Final board→iPhone 60-second tests passed with exact counts and zero bad bytes:
BLE 3,233,000 B / 60.076971 s = 0.430514 Mbps; BR/EDR 6,041,600 B / 60.076495 s =
0.804521 Mbps. Historical failures below describe the pre-fix firmware. These
single-direction tests do not establish long-term/full-duplex stability. See
[GATT control report](https://github.com/zxf1023818103/flash-studio-ios/blob/main/GATT_CONTROL_FIX.md).

## iPhone BLE GATT throughput

`GATT_LE=ON` is an isolated BL618DG validation mode, mutually exclusive with
`GATT_BR=ON`, SPP and Wi-Fi coexistence. The default remains PAN. Build into a
separate output directory:

```sh
make -C project/bl618dg_pan whole GATT_LE=ON GATT_BR=OFF HOST=ON SPP=OFF COEX=OFF OUT=/tmp/bl618dg-gatt-le
```

The shared GATT fixture source exposes FFF0/FFF1/FFF2/FFF3 over LE only, with
advertising name `BL618DG-GATT-LE`. The existing 32-byte GBR1 status has
transport byte **2**, versus **1** for BR/EDR. Launch the independent iOS app
with `--ble --autotest --seconds=60 --credit-window=16`; no Classic Settings
pairing is needed for this LE service. Counts, modulo-251 integrity, bounded
credits and terminal receipts follow the same rules as the Classic fixture.

Controller events confirmed 2M TX/RX PHY, 251-byte DLE, 15 ms interval, latency 0,
supervision timeout 4 seconds and ATT MTU 517. The app used 244-byte payloads.
On iPhone 13 Pro / iOS 27.0.1, 10-second tests passed at 0.387 Mbps phone→board
and 0.502 Mbps board→phone, zero bad bytes/count mismatches. Final short
regression also passed at 0.329 / 0.406 Mbps, showing run-to-run variation. A 60-second
phone→board run passed at 0.386 Mbps (2,904,576 bytes), but the reverse run
**failed after about 31.7 seconds**: 1,907,592 bytes had arrived with no bad bytes
before disconnect. An ATT fixed-channel congested/PDU-drop warning preceded
disconnect; its root cause is not established. Do not report the partial run
as a passing 60-second throughput result.

Native CoreBluetooth readiness also stalled after one 244-byte packet on this
confirmed LE session. The positive test uses explicit bounded board receipts,
not that readiness flag. These are current fixture measurements, not the
chip's maximum BLE throughput. See the iOS repository's
[BLE validation report](https://github.com/zxf1023818103/flash-studio-ios/blob/main/BLE_VALIDATION.md).

## iPhone local PAN TCP throughput

The standalone PANU configuration also connects to iPhone Personal Hotspot
NAP. Flash with `GATT_BR=OFF SPP=OFF COEX=OFF HOST=ON` into its own output
directory. Enable Personal Hotspot and Bluetooth on the phone, connect with
`pan_connect <phone Bluetooth address>`, and use the DHCP gateway reported by
`pan_status`. The independent [Flash Studio iOS app](https://github.com/zxf1023818103/flash-studio-ios)
provides a TCP peer on port 5001 in its PAN tab; keep the app in the foreground.

```text
net_load pan0 172.20.10.1 5001 60 tx
net_load pan0 172.20.10.1 5001 60 rx
```

Replace the example gateway with the actual address. `tx` sends board→phone;
`rx` receives phone→board. Payload bytes are `0x5a`. The RX fixture validates
payload and returns `RESULT <receivedBytes> <elapsedMs> <badBytes>\n` before
shutdown so the phone uses delivered bytes rather than its socket queue count.
The serial END line also reports `bad_bytes`; TX integrity is checked by the
receiver. The older Python peer still works; it does not interpret this receipt.

On 2026-11-01, iPhone 13 Pro / iOS 27.0.1, standalone PAN delivered 0.691 Mbps
phone→board and 0.507 Mbps board→phone over sequential 60-second local TCP
runs, with zero receiver bad bytes and zero board socket errors. See the iOS
repository's [PAN validation report](https://github.com/zxf1023818103/flash-studio-ios/blob/main/PAN_VALIDATION.md)
for exact counters and limits. This does not establish full-duplex, Wi-Fi
coexistence, long-term stability or a maximum PAN rate.

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

## BL616 build entry

`project/bl616_bluedroid` reuses these sources with single-core BL616 settings.
See its README and validation record: the attached BL616 reports Classic
Bluetooth unavailable, so PAN/SPP throughput has not been validated there.
BL618DG default build paths and GPIO16/RF behavior are preserved.

## GATT over BR/EDR validation

Build with `make whole GATT_BR=ON OUT=/tmp/bl618dg-gatt-br-build` and flash
that directory's `whole_flash_data.bin` with `tools/flash.ps1`. This mode
requires `HOST=ON SPP=OFF COEX=OFF`; it initializes neither Wi-Fi nor PAN/TCP.
The upstream host requires its BLE/GATT build switches to initialize GATT,
but the test service is registered for BR/EDR only. Its SDP record advertises
ATT on L2CAP PSM 31, with service UUID `fff0`, RX `fff1`, notification TX
`fff2`, and control/status `fff3`.

On Linux with the physical Bluetooth USB controller passed through, run:

```sh
sudo python3 tools/gatt_br_client.py B4:E8:42:3C:B5:2B --seconds 20 --block 512 --secure
```

The client discovers attributes, checks the greeting, enables notifications,
and measures both directions using a deterministic byte pattern. It requires
both endpoint counts to match and checks every received payload byte. `--secure`
requires BR/EDR encryption; an agent/pairing setup may be needed on a fresh
host. The server uses the validation application's existing Just Works policy;
it does not require encryption itself. Record `btmon` alongside a test to
verify the ACL transport, PSM and encryption. This fixture is not a production
pairing or access-control policy.

On 2026-11-01, VMware Ubuntu 26.04 / Linux 7.0 / BlueZ 5.85 successfully
used the physical Qualcomm `2c7c:0130` adapter. The final image's 20-second,
512-byte encrypted run delivered 1,049,600 bytes from board to Linux
(0.420 Mbps over the configured TX window), and 3,160,576 bytes from Linux
to board (1.245 Mbps over 20.309 seconds, including the completion fence).
Both endpoint counters matched, with zero bad bytes and zero server TX errors.
An earlier 10-second run reached 0.710 / 1.146 Mbps; throughput varies between
runs. The notification test additionally waits one second to drain queued data;
its reported TX rate uses the configured transmission window, not that drain.
HCI captured Classic ACL connections, PSM `0x001f`, and E0 encryption; the
Linux security option reported level 2 and a 16-byte key.
Reconnecting to the final image and repeating a 5-second, 244-byte encrypted
run also passed: 259,616 notification bytes (0.415 Mbps) and 352,824 write
bytes (0.528 Mbps including the fence), with matching counts and no bad bytes.

Raw serial, flash, client and HCI evidence is stored locally under the ignored
`build/validation/gatt_br` directory. Default PAN and standalone SPP still
compile with `GATT_BR=OFF`.

The independent [Flash Studio iOS client](https://github.com/zxf1023818103/flash-studio-ios)
was also tested with iPhone 13 Pro / iOS 27.0.1. The fixture explicitly includes
GAP/GATT/FFF0 in Classic EIR, keeps the GAP device name consistent, and requests
Active mode for its test connection. The optional command `STATUS_NOTIFY 1`
sends terminal `GBR1` counts on FFF2 after payload or STOP; the modulo-251 raw
pattern cannot begin with that signature. Legacy Linux clients leave this
extension disabled and continue reading status from FFF3.

Three upload-first 10-second-per-direction iPhone runs passed exact counts and
payload checks: board-to-phone 0.920–1.205 Mbps; phone-to-board 0.111–0.112 Mbps
using response-bearing writes, including final acknowledgment/drain time.
The initial system-readiness-gated iPhone writer stopped after one 512-byte
packet, even with no preceding download. Bulk downloads also sometimes blocked later outgoing GATT
operations, so the iOS benchmark uploads first, downloads last and releases the
session. The underlying interaction remains unresolved; arbitrary full-duplex
streaming, long-duration stability and concurrent Wi-Fi are not validated.

The later `RXC seconds block credits` extension enables cumulative board
receipts on FFF2 while receiving. The phone refills only board-confirmed space;
the default iOS window is 16 packets / 8 KiB, with the same final fence and byte
checks. This avoids relying on the tested Classic session's inaccurate
`canSendWriteWithoutResponse` flag. Upload improved to 0.73–0.82 Mbps in short
runs and 0.770 Mbps over a passed 60-second / 5,783,552-byte run, zero errors.
The 32-packet diagnostic stalled and is not a default UI mode. A subsequent
60-second download disconnected after about 31 seconds and was marked aborted.
See the iOS repository's `ACK_TIMING.md` for controls, negative cases and
reproduction. Original RX/TX Linux commands leave cumulative receipts disabled.

## MCU gzip and equivalent UART throughput

`GATT_BR=ON` also builds a private gzip validation worker using the repository's
zlib sources. It does not change the shared zlib component or enable compression
in PAN/SPP or other platforms. Build/flash as above, copy both
`tools/gatt_br_client.py` and `tools/gatt_gzip_client.py` to the Linux peer, then run:

```sh
sudo python3 gatt_gzip_client.py B4:E8:42:3C:B5:2B --profile text --baud 2000000 --seconds 60 --level 6
sudo python3 gatt_gzip_client.py B4:E8:42:3C:B5:2B --profile random --baud 460800 --seconds 30 --level 1
```

These are real MCU compression / encrypted BR/EDR transmission / host gzip
decompression and byte-for-byte verification tests. Input is generated on the
board with an independently timed **synthetic 8N1 UART source**, not received
through UART pins or DMA. Text consists of 64-byte ASCII log lines containing
varying counters, ADC values and state. The random profile uses a seeded
xorshift32 stream; it must not be replaced with a repeating counter when
evaluating incompressible data. No 16 MB PSRAM is required for this fixture.

Each independently compressed block contains at most 4096 raw bytes, with gzip
windowBits 15 and memLevel 5. Expanded data is sent raw. The 24-byte record header
carries sequence, raw offset, raw length, payload length and codec. GATT
notifications fragment this stream at 512 bytes. An ordered terminal record
precedes the final control/status read. There is a bounded four-record output
queue; compression runs outside BTU. The source arrival backlog is calculated
from time and consumed raw bytes, without allocating a physical UART receive
ring. `--baud 0` disables pacing to measure capacity.

`verified` requires exact raw and wire counts, deterministic payload equality,
gzip CRC verification for compressed blocks, an end marker and producer
completion without errors. `within_backlog_budget` additionally compares the
modeled raw backlog with a configurable budget (`--backlog-limit`, default
65536 bytes). This budget is an assessment threshold, not a real allocated
receive buffer. Counts can match after delayed draining even when the requested
UART rate is unsustainable. Check producer completion time and backlog as well
as counts. Equivalent baud uses 10 bits per raw byte and includes final drain
in the host's elapsed time. The `gzip_cpu_us` statistic measures wall time
inside compression calls, including preemption, rather than exact CPU cycles.

The SDK's `pvPortMalloc` selects OCRAM, whereas its free-heap query reports all
heaps. The initial compression workspace exhausted OCRAM. This fixture now
allocates compression buffers and workspace explicitly from WRAM with `kmalloc`,
preserving OCRAM for RTOS/BT tasks. Startup/free-heap statistics are still total
heap figures. The producer's serial DONE line may precede wireless draining;
the final host/server status counts are authoritative.

Raw client results, flash verification and diagnostic logs are retained locally
in the ignored `build/validation/gzip` directory. This fixture does not qualify
UART RX/DMA, iOS performance, arbitrary log compression ratio or Wi-Fi coexistence.

Final-image measurements on the attached BL618DG / Qualcomm Linux peer,
2026-11-01 (all rows passed exact payload/count verification):

| Source / gzip level | Requested 8N1 baud | Input window | Verified raw bytes | Wire bytes incl. framing | Peak modeled raw backlog | Producer completion |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Varying text / 1 | 2,000,000 | 60 s | 12,000,000 | 2,652,743 | 82,344 B | 60.005 s |
| Varying text / 6 | 2,000,000 | 60 s | 12,000,000 | 2,232,080 | 81,656 B | 60.009 s |
| Varying text / 1 | 2,500,000 | 30 s | 7,500,000 | 1,658,009 | 4,586 B | 30.012 s |
| Varying text / 1 | 3,000,000 | 30 s | 9,000,000 | 1,989,550 | 176,080 B | 30.009 s |
| Varying text / 6 | 3,000,000 | 60 s | 18,000,000 | 3,348,197 | 129,500 B | 60.015 s |
| Random / 1, raw fallback | 460,800 | 30 s | 1,382,400 | 1,390,536 | 4,177 B | 30.015 s |
| Random / 1, raw fallback | 921,600 | 15 s | 1,382,400 | 1,390,536 | 268,288 B | 18.636 s |

Capacity runs with no input pacing delivered 230,984 raw B/s for text / level 1
and 55,298 raw B/s for random / level 1, including final drain. Radio throughput
varies: the 921,600 random run delivered all bytes only after additional drain,
and does not qualify as real-time 921,600 baud. Text compresses to about 22.1%
at level 1 and 18.6% at level 6 for this source; random data does not compress.
Peak backlog is a lower bound on required receive buffering: add in-progress
and queued blocks plus margin. A 256 KiB receive ring is a reasonable next
hardware-validation target for the observed text bursts, but is not allocated
by this fixture. Do not infer physical UART zero-loss operation from this table.

At 4,000,000 baud equivalent, text / level 6 exceeded the limit: only
7,946,240 of 8,000,000 requested raw bytes were delivered before the 5-second
drain allowance expired. Peak modeled backlog reached 1,651,200 bytes; the
producer reported its deadline error, and host verification correctly failed.
Its delivered rate including drain was 317,559 raw B/s (3.176 Mbaud equivalent).
Treat 3 Mbaud as an achieved text-specific test point with significant buffering,
not a guaranteed rate for arbitrary content or radio conditions. A 2 Mbaud text
target with receive buffering has more margin; random 460,800 baud passed,
while random 921,600 baud needed delayed draining.

## zstd and LZ4 comparison

The same fixture supports `--codec gzip|zstd|lz4`. The optional fifth argument to
the `GZ` control command is the codec ID: 1 gzip (the compatible default),
2 zstd, 3 LZ4. Record payload ID 0 still means raw fallback. Status now appends
codec ID and peak codec workspace bytes to the original 56-byte prefix, for
64 bytes total; the updated host accepts either length.

```sh
sudo python3 gatt_gzip_client.py B4:E8:42:3C:B5:2B --codec zstd --level 1 --profile text --baud 2000000 --seconds 30 --backlog-limit 262144
sudo python3 gatt_gzip_client.py B4:E8:42:3C:B5:2B --codec lz4 --level 1 --profile text --baud 2000000 --seconds 30 --backlog-limit 262144
```

`component/serial_codec_config` contains unmodified zstd v1.5.7 and LZ4 v1.10.0
sources, their licenses and immutable upstream commit IDs in `UPSTREAM`. This
component is explicitly included only by the standalone GATT validation build;
the common component registry and existing platform projects are unchanged.
zstd is single-threaded, uses a 4 KiB window (`windowLog=12`), and enables its
frame checksum. LZ4 uses independent raw blocks through
`LZ4_compress_fast_extState`; for LZ4, `--level` is acceleration, with 1 the
normal default, not an HC compression level. The host uses system libzstd and
liblz4 through Python ctypes. Every decoded byte is compared with the source,
including raw fallback and LZ4 blocks without a frame checksum.

All codec allocations use aligned WRAM storage. The peak workspace counter
measures requested live codec allocation bytes, excluding allocator overhead,
input/output queues and task stacks. It is distinct from the total free-heap
low watermark. Compression-call wall time includes RTOS preemption and is not
a CPU utilization counter. Only one codec context is allocated at a time.

Same-firmware 2 Mbaud / 30-second text comparison on 2026-11-01, with 6,000,000
raw bytes verified for every row and zero producer errors:

| Codec | Wire bytes including record framing | Wire/raw | Codec workspace | Compression-call wall time | Peak modeled raw backlog |
| --- | ---: | ---: | ---: | ---: | ---: |
| gzip 6 | 1,115,972 | 18.60% | 153,284 B | 14.219 s | 81,640 B |
| zstd 1 | 754,291 | 12.57% | 70,144 B | 8.827 s | 4,464 B |
| zstd 3 | 762,174 | 12.70% | 86,528 B | 9.757 s | 4,760 B |
| LZ4 fast, acceleration 1 | 2,011,192 | 33.52% | 16,416 B | 2.528 s | 4,488 B |

Text capacity runs, including final drain, measured gzip 6 at 2.98-3.03 Mbaud,
zstd 1 at 3.55-3.96 Mbaud, zstd 3 at 3.45 Mbaud, and LZ4 at 1.87-1.88 Mbaud.
These unpaced measurements are not strict hardware ceilings: radio and queue
behavior vary, and paced operation can differ, as demonstrated by the LZ4
2 Mbaud run above. Random data expanded and was sent raw for all three codecs;
observed end-to-end rates were about 74-97 kB/s across sequential capacity runs.
Those differences do not establish an algorithm-specific RF speed advantage.

Long and boundary runs used a 256 KiB modeled raw-backlog budget. All payloads
were received and verified, including the two overload cases; delayed delivery
does not qualify as real-time operation:

| Codec / input | Input window | Raw bytes | Peak modeled backlog | Producer completion | Assessment |
| --- | ---: | ---: | ---: | ---: | --- |
| zstd 1 / 3.5 Mbaud | 60 s | 21,000,000 | 206,890 B | 60.007 s | Within budget and input window |
| zstd 1 / 4 Mbaud | 30 s | 12,000,000 | 20,128 B | 30.012 s | Short run passed |
| zstd 1 / 4 Mbaud | 60 s | 24,000,000 | 468,640 B | 60.356 s | Exceeds budget; additional drain |
| zstd 1 / 5 Mbaud | 20 s | 10,000,000 | 1,912,996 B | 24.872 s | Exceeds budget; 4.872 s additional producer drain |
| LZ4 fast / 2 Mbaud | 60 s | 12,000,000 | 95,488 B | 60.003 s | Within budget and input window |

For this sample, zstd 1 offers the best combination of wire size, codec memory
and achieved equivalent UART rate. zstd 3 did not improve this sample. LZ4 uses
the smallest workspace and shortest compression-call time but transmits more
data. A conservative product target remains 2 Mbaud text with receive buffering;
3.5 Mbaud is an achieved zstd test point, while 4/5 Mbaud require more buffering
or delayed delivery. An intentional mid-stream zstd disconnect followed by a
fresh 2 Mbaud run also passed, exercising context/queue cleanup. Original raw
GATT and standalone SPP regression results are retained with the codec logs.

Raw measurements and the flashed-image hash are retained locally in the ignored
`build/validation/codecs` directory. These results describe this synthetic text
and random input, 4 KiB independent blocks, and a Linux BR/EDR peer. Physical
UART RX/DMA and iOS remain outside this benchmark.

### Isolated SoftAP validation with RouterOS station

`make whole SOFT_AP=ON OUT=/tmp/bl618dg-softap-build` builds a BL618DG-only,
Wi-Fi-only test image. It uses FreeRTOS+TCP, WPA2-PSK SSID `BL618DG-AP-Test`
(test key `bl618dg-test`), channel 36 / 5180 MHz / 20 MHz, and a fixed AP endpoint
`192.168.77.1/24`. The test DHCP server provides four MAC-bound 120-second leases
starting at `.100`. This small DHCP fixture is not a production DHCP service.
HTTP port 8080 serves deterministic files, validates uploads byte by byte, and
reports AP events, DHCP counters and available heap at `/status`. Byte counters
are 32-bit and wrap at 4 GiB. Normal STA/PAN/SPP builds keep this feature disabled.

The RouterOS fixture temporarily uses the idle 5 GHz `wifi2` radio as a station;
it keeps wired management and the active 2.4 GHz `wifi1` AP. It excludes wifi2
and its virtual APs from the LAN bridge and installs only test-specific DHCP
and NAT entries, with no default-route or DNS changes. The host at
`192.168.100.6` accesses the board through `192.168.100.2:18080`; file contents
pass through router RAM and are never stored on its flash. The helper snapshots
the original Wi-Fi configuration and installs a one-hour automatic rollback.
Run setup only after confirming these interface names, addresses and client
assignments match the intended router. Always restore explicitly after testing.

```sh
python3 tools/routeros_ap_test.py setup
python3 tools/routeros_ap_test.py status
python3 tools/ap_stability.py --rounds 30 --out build/validation/softap/transfers.jsonl
python3 tools/ap_stability.py --rounds 0 --reconnects 20 --aborts 10 \
  --out build/validation/softap/recovery.jsonl
python3 tools/routeros_ap_test.py restore
```

Each transfer defaults to 2 MiB with byte `i % 251`; the host verifies SHA-256
on downloads and the board checks every upload byte. Recovery tests deliberately
disconnect the RouterOS station, then verify fresh transfers, and optionally
close HTTP transfers before their declared length. Intentional aborts increase
the board's HTTP failure counter and must be distinguished from spontaneous
failures. The fixture is single-client and does not exercise flash programming,
physical UART capture, concurrent Bluetooth, DFS, mobile roaming or iOS.

Initial AP association exposed an adapter bug: FHOST's forwarded RX path reads
Ethernet and payload TX segments separately, while the port supplied one segment.
The port now supplies the two segments for forwarded frames; normal outgoing
traffic retains the single-segment path. This fixes the first broadcast IPv4
packet crashing inside `fhost_tx_req_do` after successful WPA2 association.

2026-11-01 measurements used our SDK/adapter on the current BL618DG board and
a RouterOS 7.22.3 hAP ac2 station, with no Bluetooth or PSRAM in the test image.
The host download SHA-256 was
`1e075c8d478ad21844e33e830a695ef03a4d2488b69ee275bd8947618bb1be1e`.
Rates below include the HTTP request and response time for 2 MiB payloads:

| Stage | Download payload rate | Upload payload rate | Integrity |
| --- | ---: | ---: | --- |
| Channel 36, initial 30 pairs / 120 MiB | 4.609 Mbps | 4.596 Mbps | All passed |
| Channel 36, after AP stop/start, 3 pairs | 0.502 Mbps | 0.456 Mbps | All passed |
| Channel 36, after board reset, 3 pairs | 0.500 Mbps | 0.444 Mbps | All passed |
| Channel 44, immediate 3-pair comparison | 4.896 Mbps | 4.869 Mbps | All passed |
| Later return to channel 36, 1 pair | 4.471 Mbps | 4.445 Mbps | Passed |
| Final return to channel 44, 1 pair | 5.097 Mbps | 4.994 Mbps | Passed |

The first channel-36 recovery run completed all 20 station reconnects and fresh
bidirectional transfers. HTTP readiness took a median 4.644 s, maximum 16.078 s,
including the deliberate 2 s station outage. The final reconnect cycle took
79.110 s because the subsequent transfers became slow. Two subsequent HTTP
abort/recovery trials also delivered correct data but took about 70 s each;
the planned ten-trial abort run was stopped during its third trial to investigate
the persistent slowdown. This interrupted run must not be reported as a complete
pass. Board HTTP failure counters include deliberate aborts and the interrupted
test connection; no corrupt payload was observed.

In the slow state, RouterOS reported approximately -45 dBm and 52-72 Mbps PHY
rates, low CPU load and no simple/tree queues or mangle rules. RouterOS directly
fetched and discarded a 2 MiB file in 28 s (about 0.60 Mbps), reproducing the
slowdown without Windows/WSL or host NAT. AP restart and board reset on channel 36
did not restore speed; moving both AP and station to channel 44 immediately did.
Returning later from channel 44 to channel 36 also achieved normal throughput
(4.471/4.445 Mbps for a fresh pair). The recovery coincided with changing channel;
these measurements do not isolate RF contention, time-varying interference or
PHY/driver behavior, or establish a permanent channel-36 limitation. Reconnect
count alone does not establish the cause. DHCP leases renewed and no spontaneous
firmware reset or payload mismatch was observed in these runs.

The same workload on channel 44 completed all 20 reconnects and all ten HTTP
abort/recovery trials, verifying 120 MiB of fresh complete transfers. Median
HTTP readiness was 4.651 s and maximum 17.020 s, including the 2 s forced outage;
the longest reconnect-plus-transfer cycle was 23.640 s. Abort-plus-fresh-transfer
cycles took 6.879-7.416 s. No persistent throughput degradation or payload errors
were observed in that run. The ten deliberate HTTP failures were counted as
expected, with zero authentication errors or spontaneous firmware resets.

2 Mbaud 8N1 produces 200,000 payload bytes/s, requiring at least 1.6 Mbps of
network payload throughput before additional framing and operating margin.
The healthy runs exceed this rate, while the channel-36 slow state does not.
`ap_stability.py --min-mbps 1.6` adds that rate check to ordinary transfer trials;
without it, `ok` describes payload integrity, not real-time UART suitability.
These results support further SoftAP development with non-DFS channel selection,
buffering and explicit overload handling, but do not establish lossless physical
2 Mbaud UART operation or long-duration/iOS qualification. Raw logs, image hash
and per-stage summaries are retained under `build/validation/softap`.
RouterOS Wi-Fi settings were restored and compared with the pre-test export;
temporary DHCP/NAT/list entries, rollback scheduler and restore script were
removed. The wired default route and existing wifi1 client remained active.
The board was left running the channel-44 test AP; a board reset returns this
test image to its compiled channel-36 default.

## NimBLE LE L2CAP CoC

This opt-in BL618DG fixture reuses `component/bouffalo_sdk/nimble_port`
(the existing structured on-chip HCI transport and FreeRTOS NPL) and
`component/nimble_config/nimble` 1.9.0. It does not introduce another host port
or modify the vendored NimBLE source.

```sh
make -C project/bl618dg_pan whole NIMBLE_COC=ON OUT=/tmp/bl618dg-nimble-coc
```

`NIMBLE_COC=ON` defaults HOST to OFF and rejects other host/radio test modes or
CHIP=bl616. The mode starts combo RF, fixes GPIO16 HIGH for the shared antenna,
and starts only NimBLE BLE; no Wi-Fi or TCP/IP stack runs. Other builds retain
their existing defaults. Only this configuration disables Mesh, enables one
CoC channel (MPS 247), and expands msys1 from 24 to 64 blocks. The standalone
latest-SDK fixture uses upstream tinycrypt because it does not link the older
application's mbedTLS/CLI targets; normal port builds keep their crypto glue.

Scan `BL618DG-CoC` in Flash Studio's L2CAP tab. The service UUID is
`B1F01000-97B3-4E38-BAF4-91AFB3CD92F0`; FFF4 is a read-only two-byte
little-endian PSM (0x0081). GATT only discovers that PSM. Bulk payloads and
receipts use the LE dynamic L2CAP channel via CoreBluetooth input/output
streams. There is no Classic L2CAP or GATT throughput fallback. This is an
unencrypted development fixture, not an authenticated firmware update service.

The framed stream bounds payloads to 4096 bytes, handles partial/coalesced
reads, checks offset-modulo-251 payloads, and requires receiver counts and a
terminal receipt in both directions. Disconnect, backgrounding, stream errors
or timeout invalidate incomplete runs. TX retries EBUSY, transfers ownership on
accepted/stalled SDUs, and waits for the unstall event before continuing.
TX runs on the host event queue, not inside the credit callback. Receive SDUs
use a dedicated bounded pool; no complete recording is buffered in RAM.

See the iOS repository's `L2CAP_VALIDATION.md` for the wire format, measured
results and limits. The raw logs/JSON are retained with local validation
artifacts rather than checked into the SDK.

Final 60-second CoC validation (2026-11-02 CST, iPhone 13 Pro / iOS 27.0.1):
iPhone→board 1,776,640 bytes / 60.355531 s = **0.235490 Mbps**;
board→iPhone 2,698,240 bytes / 60.298263 s = **0.357986 Mbps**.
Both receiver counts matched, bad bytes and peer errors were zero, and neither
direction disconnected. LE 2M PHY and 15 ms interval were confirmed by logs.
The final 709,584-byte image passed flash SHA verification:
`436fc56c57a507cfc86fa34106043604b42e30b45b72556eebd70a8dec0dba85`.
This baseline is slower than the previous GATT tests; it does not establish a
CoC or chip throughput limit. No physical UART or Wi-Fi concurrency was tested.

## Bluedroid LE L2CAP CoC

```sh
make -C project/bl618dg_pan whole BLUEDROID_COC=ON OUT=/tmp/bl618dg-bluedroid-coc
```

This independent BL618DG mode reuses `component/bluedroid_config` and its
vendored host's existing LE CoC implementation. It starts combo RF and GPIO16
HIGH, without Wi-Fi or TCP/IP. `HOST` defaults OFF; conflicting PAN, SPP, GATT,
NimBLE, Wi-Fi/coexistence and non-BL618DG modes are rejected. Other platform
and default host configurations keep CoC disabled.

The advertising name is `BL618DG-CoC-BD`, service UUID
`B1F01001-97B3-4E38-BAF4-91AFB3CD92F0`, with FFF4 exposing PSM 129 as a
two-byte little-endian value. The separate service UUID lets Flash Studio
identify Bluedroid rather than label a cached NimBLE service incorrectly.
The L2C1 stream framing and receiver-confirmed counters match the NimBLE test.
Use the L2CAP tab or launch with `--l2cap --bluedroid-coc --autotest --seconds=60`.

Board MTU/MPS are 2048/247, initial receive credits 24; the iPhone negotiated
1251/1251 and offered 65535 initial credits. The development PSM explicitly
registers BTM_SEC_NONE. Encrypted access and firmware authorization are outside
this benchmark. Link logs confirm LE 2M and interval 15 ms.

All sends run on BTU, never inside congestion callbacks. A BTU-local queue
adapter stops submitting SDUs at ACL queue length 8; HCI fragmentation can add
segments beyond that threshold (the successful short run peaked at 12).
CoC credits alone cannot bound that host-side queue. The adapter deliberately
uses the vendored host's private `l2c_int.h` and must be reviewed when updating
Bluedroid. TX SDUs transfer ownership on all Bluedroid return codes; RX SDUs
are released after parsing. Framing buffers and pending worker events are bounded.

See the iOS repository's `BLUEDROID_L2CAP_VALIDATION.md` for test results,
comparison with NimBLE and the initial ACL-backlog failure excluded from passing
results. SDK changes remain local; raw evidence is retained in ignored local
validation artifacts.

Final Bluedroid CoC 60-second test (2026-11-02 CST, iPhone 13 Pro / iOS 27.0.1):
phone→board **0.441228 Mbps** (3,324,928 B / 60.284953 s), board→phone
**0.339938 Mbps** (2,553,856 B / 60.101712 s). Both receiver counts match;
bad bytes and peer errors are zero, with no disconnect or allocation failure.
ACL queue peak was 12 after HCI fragmentation, and final heap was 516,972 B.
The final 1,102,992-byte image passed host/device flash SHA verification:
`7d0b48aff7489b194f6fb1ca0b1d708e5faa0938bd581059a32e05102bbee996`.
The previous NimBLE CoC baseline was 0.235490 / 0.357986 Mbps. Its initial RX
credit window was 9 rather than 24, so this is a configuration comparison,
not a controlled measurement of intrinsic host-stack speed.

## NimBLE CoC matched-setting retest

The NimBLE fixture now matches the Bluedroid test's explicit settings: receive
credit/refill window 24 (previously 9), MTU/MPS 2048/247, block and SDU cap
1024 bytes, TX queue pause threshold 8, up to four SDUs per 2 ms callback,
2M PHY / 15 ms interval / zero latency, Wi-Fi off and the same L2C1 protocol.
Build with `NIMBLE_COC=ON OUT=/tmp/bl618dg-nimble-coc-matched`.

The credit window is set in the ACCEPT callback before the connection response;
only this fixture uses private NimBLE channel fields. Its TX adapter counts
`bhc_tx_q` under the host lock and retains the msys reserve. No upstream NimBLE
source or other port defaults are changed. Private-header compatibility must be
reviewed on NimBLE updates. NimBLE's mbuf queue and Bluedroid's HCI-fragment
queue have different accounting, even though their pause threshold is the same.

To prevent iOS reusing Bluedroid's GATT handle cache after reflashing, the CoC
fixture derives a static random identity from the factory public address.
Other NimBLE builds retain their address behavior. An initial cached-service
PSM-read failure was excluded from measured runs. Flash Studio must be launched
without `--bluedroid-coc` and its JSON must identify the actual NimBLE service.

The matched-setting 10-second smoke test passed at 0.458965 Mbps phone→board
and 0.403034 Mbps board→phone, with matching counts and zero errors. Actual
credits were 24/65535, TX queue peak 8, and msys free count returned to 64.
See the iOS repository's `NIMBLE_MATCHED_VALIDATION.md` for the final long run.

Matched-setting NimBLE final 60-second result: phone→board **0.493847 Mbps**
(3,718,144 B / 60.231556 s), board→phone **0.446500 Mbps**
(3,356,672 B / 60.141907 s). Both counts match with zero bad bytes / peer errors
and no disconnect. TX queue peak 8; all 64 msys blocks free at the end.
The 709,584-byte final image passed flash host/device SHA verification:
`33a9d9b68ceeebb5fcbd653dab4e2d8774cb245aa59f0d224aef9d24fb6356f2`.
This single aligned-setting run exceeded the earlier Bluedroid CoC result
(0.441228 / 0.339938 Mbps). Both credit and queue controls changed from the
original NimBLE baseline, so do not attribute the gain to credits alone.

## NimBLE GATT over BLE

```sh
make -C project/bl618dg_pan whole NIMBLE_GATT=ON OUT=/tmp/bl618dg-nimble-gatt
```

This isolated BL618DG configuration reuses the existing NimBLE port, RF startup
and structured HCI transport. No Wi-Fi or TCP/IP starts. It selects MTU 517,
64 msys blocks, Mesh off and CoC off; all other platform defaults are retained.
Conflicting host/radio modes and other chips are rejected. The shared startup
file is named `main_nimble_coc.c`, but selects the GATT fixture under this flag.

The service UUID is `B1F02000-97B3-4E38-BAF4-91AFB3CD92F0`, name
`BL618DG-GATT-NB`, with FFF1 write, FFF2 notify and FFF3 read/write control.
`component/nimble_config/ble_gatt_bench.c` implements the same GBR1 transport=2
status, cumulative RXC receipts and modulo-251 payload checks as the Bluedroid
GATT test. The iOS app selects it with `--nimble-gatt --autotest --seconds=60`.
A separate derived static random identity avoids reusing the CoC/Bluedroid
GATT handle cache. Pairing is not required; this is an unencrypted test fixture.

All state and notification sends run on the host event queue. A 2 ms callback
prioritizes status and sends up to four notifications, pausing at eight queued
host packets or the msys reserve. Accepted notification bytes count only once;
receiver counts and terminal status determine pass/fail. The NimBLE adapter
uses private host queue headers, which require review on host updates.

The initial 10-second test passed at 0.361263 Mbps phone→board and
0.421416 Mbps board→phone, both with matching counts and zero bad bytes/errors.
Actual link parameters were 2M PHY, interval 15 ms, MTU 517; application payload
244 bytes and cumulative upload window 16 packets, matching the earlier
Bluedroid GATT/BLE application test. See the iOS repository's
`NIMBLE_GATT_VALIDATION.md` for the final long run and comparison.

Final NimBLE GATT/BLE 60-second result: phone→board **0.365209 Mbps**
(2,744,512 B / 60.119261 s), board→phone **0.452008 Mbps**
(3,393,064 B / 60.053120 s). Both receiver counts match, bad bytes and peer
errors are zero, with no disconnect during transfer. App teardown afterward
was normal. The verified 713,520-byte firmware SHA-256 is
`11dc5f29b5c737ab1a20ef31ce3756a8b712270fddf358af097a8e8acbfe8a4e`.
The earlier Bluedroid GATT/BLE measurement was 0.359482 / 0.430514 Mbps,
close to this run. No general chip-limit or stack-ranking conclusion follows
from these individual measurements.
