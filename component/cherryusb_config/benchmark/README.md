# Module-to-module CherryUSB Bulk benchmark

The same vendor-class protocol runs on the BL618DG and AmebaGreen2 fixtures.
Each firmware selects a fixed USB role. Swap the firmware roles to measure both
host controllers. UART carries commands and results; payload travels only over
the USB cable between the modules.

The lab VID:PID is `ffff:b017`, interface class/subclass/protocol `ff:42:01`.
Bulk OUT is EP5 and Bulk IN is EP4. Descriptors advertise 512-byte packets at
High-Speed and 64-byte packets at Full-Speed; results report the actual speed
from the host stack (`3` = High-Speed, `2` = Full-Speed).

On the host console:

```
usbbench
usbbench run 64 16384 1 3
```

Arguments are MiB **per direction per round**, block bytes (power of two,
512 through 16384), full payload checking (0 or 1), and round count.
The example transfers 64 MiB host-to-device then 64 MiB device-to-host, three
times. It measures each direction separately, not simultaneous duplex traffic.

Every block contains a magic value, sequence number, byte offset and length.
The remainder uses a deterministic 32-bit pattern. Headers are always checked;
with `verify=1`, every received payload word is checked as well. Verification
cost is included in elapsed time. An endpoint-zero status read confirms the
receiver's byte/packet/error counters after each transfer. Only matching counts
and zero errors produce `ok:1`. No result is inferred from the nominal bus rate.

`USB_BENCH_RESULT` lines contain JSON. `kbps = bytes * 8 / elapsed_ms` uses
decimal kilobits per second; divide by 1000 for Mbps. USB control setup and the
final status query are outside the timed bulk interval. Firmware processing,
DMA, interrupt handling, host scheduling and optional payload checks are inside.

Green2 uses the vendored official CherryUSB v1.6.1 core/DWC2 driver and an
extracted Realtek PHY library, with DMA buffers in the existing noncacheable
region. BL618DG uses its pinned SDK's CherryUSB v1.6.1 core and EHCI/DMA driver.
No computer acts as USB host or a throughput relay during these measurements.

## Build and run

From the repository root, build each role into a separate directory:

```sh
make -C project/bl618dg_usb_bench whole ROLE=host -j8
make -C project/bl618dg_usb_bench whole ROLE=device -j8
make -C project/amebagreen2 USB_BENCH=ON USB_ROLE=host MINIMAL=ON \
  BUILD="$PWD/build/usb-throughput/green2-host" FW_VERSION=usb-green2-host APP_BUILD=9
make -C project/amebagreen2 USB_BENCH=ON USB_ROLE=device MINIMAL=ON \
  BUILD="$PWD/build/usb-throughput/green2-device" FW_VERSION=usb-green2-device APP_BUILD=8
```

Run the two BL618DG image-packaging commands sequentially: the SDK packager
uses shared scratch files. Green2's default is `USB_BENCH=OFF`.
When exchanging roles, flash the future **device** first, then its host.
Use the board projects' flashing instructions with COM37 for BL618DG and
COM15 / COM19 for Green2. The UART baud rates are 500000 and 1500000 respectively.

On Windows, from the repository root with Python and pyserial installed:

```powershell
python component/cherryusb_config/benchmark/run_benchmark.py --port COM37 --baud 500000 --host bl618dg --output build/usb-throughput/bl618dg-host-results
# After exchanging firmware roles:
python component/cherryusb_config/benchmark/run_benchmark.py --port COM15 --baud 1500000 --host amebagreen2 --output build/usb-throughput/green2-host-results
```

The runner checks enumeration, runs 1 MiB per direction with 512-byte and
4096-byte blocks, then three rounds of 64 MiB per direction with 16384-byte
blocks. All runs enable full payload verification. It saves `uart.log` and
`results.json`, checks counts and throughput arithmetic, and exits unsuccessfully
on a failed run or timeout. Stop other serial monitors before opening the ports.

BL618DG's fixture applies a local EHCI completion workaround, documented in
its project README. It can affect measured throughput; these results describe
the tested firmware and hardware pair, not the controller's maximum capacity.

See [the COM37 / COM15 validation report](VALIDATION.md) for measured results.
