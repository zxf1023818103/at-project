# Single-radio throughput assessment — 2026-11-01

**Completed:** 12 TCP load runs, two 30-second runs per radio/band/direction.
No socket errors, load stalls or disconnections were observed in these runs.
The mean TX/RX rates were 0.564/0.739 Mbps for Classic PAN, 4.846/4.243 Mbps
for Wi-Fi 2.4 GHz, and 5.831/4.647 Mbps for Wi-Fi 5 GHz.

## Current status

The user restored board response after initial COM15 ROM handshake and normal
reset attempts produced no reply. Both single-radio images have since been
flashed successfully and their contents verified through COM15/COM23.
Each enables only the radio being measured, rather than leaving the other
protocol stack initialized and idle. The initial failed attempts did not write
flash; their logs are retained separately from the recovered tests.

The BLU COM17 instrument was also present. No instrument commands were sent:
its documented voltage readback does not establish output state, and metadata
queries can affect power. No existing BLU session status was found.

## Test setup

| Image | Wi-Fi stack | Bluetooth | GPIO16 |
|---|---|---|---|
| BT-only PANU | Not enabled | Controller + Bluedroid Classic PANU | High |
| Wi-Fi-only | FHOST + FreeRTOS+TCP | Not enabled | Low |

```sh
make -C project/bl618dg_pan whole COEX=OFF OUT=/tmp/bl618dg-bt-throughput-build
make -C project/bl618dg_pan whole WIFI_ONLY=ON OUT=/tmp/bl618dg-wifi-throughput-build
```

Each radio/band is tested with two 30-second TX and two 30-second RX runs using
`net_load` and `tools/tcp_load_server.py`. TX means board-to-Windows and is
measured at the Windows receiver; RX is measured at the board. Wi-Fi tests
cover 2467 MHz and 5240 MHz separately. PAN uses the Windows Bluetooth
NAP, not BLE/GATT. No traffic result from an unresponsive board is reported as
zero throughput.

The application uses FreeRTOS+TCP in both images, without lwIP. All peers are
local/on-subnet: PAN board `192.168.138.128` to Windows `192.168.138.1`, Wi-Fi
board `192.168.100.7` to Windows `192.168.100.6`. Each socket binds the selected
interface's local address. Host JSON logs independently identify those source
addresses. TX payload patterns were verified by the host. The load interval
starts after connection setup; RX counts bytes consumed by the application,
while host send-accepted bytes are not substituted for receiver throughput.

Wi-Fi uses AP `Zeng`, WPA2, channel 12 / 2467 MHz on 2.4 GHz and channel 48 /
5240 MHz on 5 GHz. Router-side band/association snapshots are archived.
No ping load was added to these throughput measurements.

## Completed measurements

Each value below is receiver-side TCP payload throughput in decimal Mbps.

| Radio / direction | Round 1 | Round 2 | Mean |
|---|---:|---:|---:|
| Classic Bluetooth PAN TX | 0.556 | 0.573 | 0.564 |
| Classic Bluetooth PAN RX | 0.746 | 0.733 | 0.739 |
| Wi-Fi 2.4 GHz TX | 4.865 | 4.826 | 4.846 |
| Wi-Fi 2.4 GHz RX | 4.254 | 4.232 | 4.243 |
| Wi-Fi 5 GHz TX | 5.965 | 5.696 | 5.831 |
| Wi-Fi 5 GHz RX | 4.723 | 4.571 | 4.647 |

The BT endpoint remained up after all four runs, with RX 5450 / TX 6799 frames
and zero adapter RX/TX drops. The 2.4 GHz endpoint remained up through its four
runs, RX 34612 / TX 48534 frames and zero adapter drops before intentional
disconnection to change band. These counters describe software adapter drops,
not over-the-air packet loss or TCP retransmissions.
After both Wi-Fi bands, cumulative RX/TX frame counts were 73822/104882, still
with zero adapter drops. All 12 native task results reported error=0 and
stalls=0; all 12 host sessions ended without errors. TX receiver byte totals
matched the board's payload totals, and host payload verification passed.

Router-side signal snapshots were -66 dBm on 2.4 GHz and -47 dBm on 5 GHz.
These are observed local-network results, not a comparison at matched signal
strength. Only one direction ran at a time. The repeat counts and durations
do not constitute long-term reliability qualification.

The independent runs did not reproduce the PAN connection/supervision
timeouts seen under concurrent Wi-Fi/BT load. This is evidence for continuing
the investigation in the coexistence/data scheduling path; it does not by
itself identify which RF, controller or host mechanism is responsible.

## Existing reference data, not strict single-radio results

The earlier combo application kept both radio stacks initialized. Its
single-traffic-flow baselines were:

| Test | Receiver TCP throughput | Interval |
|---|---:|---:|
| Wi-Fi 2.4 GHz TX; PAN not carrying TCP | 4.799 Mbps | 10 s |
| PAN TX; Wi-Fi associated but not carrying TCP | 0.373 Mbps | 15 s |

These are useful reference points, but **must not be labeled BT-only or
Wi-Fi-only throughput**. They neither establish single-radio RX performance
nor the maximum capability of the chip. See [combo evidence](COMBO_VALIDATION.md).

## TCP configuration constraint

The current project uses MSS 1460, RX and TX streams each `4 * 1460 = 5840`
bytes, 16 TCP window segment descriptors, and 16 network buffers.
`FreeRTOS_Sockets.c` initializes each RX/TX window to half its stream size
divided by MSS: thus each window defaults to **2 MSS, approximately 2920
bytes**, not the entire 5840-byte stream buffer.

This is a possible throughput constraint, especially on the higher-RTT PAN
link. A follow-up larger-window comparison is needed before interpreting the
measured application throughput as a radio limit. No larger-window throughput
numbers have been measured or inferred as actual performance here.

The shared load tool's heap statistics now call the SDK's `kfree_size(0)`
directly. Previously they depended on the Bluedroid compatibility wrapper
`xPortGetFreeHeapSize`, causing the new Wi-Fi-only load build to fail to link.
Both standalone image builds pass after the change; it changes heap reporting,
not TCP parameters or radio behavior.

## Local artifacts

Images, ELF files, resolved configurations and logs are under
`build/validation/single_radio/` (ignored by Git).
`results.json` contains per-run receiver bytes, elapsed time and rates;
`aggregate_results.py` reproduces the summary. The per-case UART files,
`server-recovered.log`, radio status snapshots, AP snapshots and build logs
provide the raw evidence.

- BT-only image SHA256:
  `7859e3b37cecbe72f8ed42ae762c28c47e34319321abebd1ce294ee5ae42857b`.
- Wi-Fi-only image SHA256:
  `2e731e16e4b65c708ddd22c53ca7ce52ec24df3d928acf9eabde07c69940e196`.

The temporary Windows TCP server is stopped when this test turn ends; private
AP credentials are removed. Router configuration and BLU power state were not
changed.
The board is left running the Wi-Fi-only image on 5 GHz, with address
`192.168.100.7`. The separate BT-only image is retained for follow-up tests.
