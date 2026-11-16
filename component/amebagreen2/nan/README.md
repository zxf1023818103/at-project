# AmebaGreen2 standalone NAN

`make -C project/amebagreen2 NAN=ON` builds Wi-Fi Aware on the two Green2
cores. KM4NS runs the radio and NAN scheduler; KM4TZ runs FreeRTOS, the hostap
PASN responder, mbedTLS crypto, LittleFS pairing storage, and FreeRTOS+TCP.
There is no Linux process, cfg80211 stack, external host, or lwIP IP stack at
runtime. The `cfg80211`/`lwip` names retained in some glue are vendor IPC/packet
ABI names only.

The profile starts automatically, publishes `_flashstudio._tcp` as **Green2
FreeRTOS**, and accepts a paired iPhone's IPv6 TCP connection on port **8000**.
Send exactly `NAN-PING\n` and read `NAN-PONG\n` before closing the connection.
The initial pairing PIN is the development value **123456** (`nan_service.c`).
The Flash Studio iOS stress client must keep its write side open while waiting
for the reply; this FreeRTOS+TCP version rejects sends after a received FIN.

## Build and operate

```sh
make -C project/amebagreen2 NAN=ON
# Optional TLSF consistency checks around authentication and shared-memory IPC:
make -C project/amebagreen2 NAN=ON NAN_HEAP_CHECKS=ON
# Optional bounded TCP metadata/timing trace, printed after each test phase:
make -C project/amebagreen2 NAN=ON NAN_NET_DIAG=ON
# Build the ordinary STA profile separately:
make -C project/amebagreen2 NAN=OFF BUILD="$PWD/build/amebagreen2-normal"
```

Artifacts are `build/amebagreen2/firmware.bin` (boot + OTA1), `Image_All.bin`
(4 MiB factory image), and the per-core AXF/map files. Use `firmware.bin` for
updates that retain pairing and calibration. `NAN=OFF` remains the default.
The ASDK 12.3.1 compiler and normal project dependencies are required.

On the COM5 board, DTR and RTS control BOOT/RESET on the same UART:

```powershell
.\project\amebagreen2\flash.ps1 -Port COM5 -BootPort COM5 `
    -Image .\build\amebagreen2\firmware.bin -Python python.exe
```

Choose the Python installation containing the project's download dependencies.
The downloader checks the chip identity and 4 MiB NOR capacity, preserves
0xe000–0xffff and LittleFS, verifies every programmed byte, and resets into the
application. Console: 1500000 baud, 8N1. Commands: `nan status`, `nan stop`,
`nan start`. Status includes event drops, link state, packet counts and PONGs.
`up`, `down` and `err` count completed upload/download phases and aborted
commands respectively.

The NAN task also prints `[NAN] AP Available Heap Size: <bytes> bytes` every
10 seconds after startup. This is the current free heap of AP/KM4TZ, from
`xPortGetFreeHeapSize()`, not the NP heap or a historical minimum. The existing
100 ms control loop checks elapsed ticks, so logging continues during events
and while idle without adding a task or allocating a periodic buffer. The
interval is `NAN_HEAP_LOG_INTERVAL_MS` in `nan_service.c`.

Pair once in the iPhone AccessorySetupKit picker, then use the saved accessory
for subsequent connections. `/nan-pairing` stores the local NIK, vendor cache,
and NIK-bound NPKs. Updates close/sync a temporary file before atomic rename.
Firmware-only flashing retains this file. Formatting LittleFS requires pairing
again on both ends. Pairing records are credentials; do not commit their contents
or raw diagnostic captures.

## Throughput test

In Flash Studio's NAN page, keep the existing Green2 pairing, choose a test
duration and start the throughput test. Both directions share one NAN IPv6 TCP
connection on port 8000. The firmware implements the App's existing
`NAN-THROUGHPUT/1` protocol directly on FreeRTOS; no host-side service is needed.

```text
NAN-THROUGHPUT/1 <test-id> UPLOAD <seconds>\n
NAN-THROUGHPUT/1 <test-id> DOWNLOAD <seconds>\n
```

The server replies `READY\n`. Each payload frame has a four-byte big-endian
length; zero ends a phase. Upload accepts frames up to 256 KiB, download sends
64 KiB frames. A JSON line then reports `version`, `test_id`, `direction`,
`bytes`, `seconds`, `mbps` and `counter` (`received` for upload, `submitted` for
download). IDs contain 1–64 ASCII letters, digits or hyphens; duration is 1–60
seconds. The next command can follow on the same connection. `NAN-PING\n`
remains supported and closes after its reply.

Only payload bytes count. The App verifies both sides' byte totals and uses
the **receiver's** interval for Mbps: firmware timing for upload, iPhone timing
for download. Firmware download counters describe bytes submitted to TCP;
the App's matching receive counter establishes their delivery. These are
application TCP rates, not Wi-Fi PHY rates.

`nan_tcp_protocol.c` streams through a 4 KiB work buffer without allocating
whole frames. The listener reserves 40/24 configured MSSes for TX/RX streams
and 32/16 for their windows, for this service only. The NAN profile has 40 AP
IPC buffers, 32 NP buffers, RX/TX aggregation limits of 16/32, 96 TCP segment
records and 32 network buffer descriptors. Other low-memory profiles retain
their defaults. Stream lengths and windows use the configured MSS of 1460;
the iPhone negotiates 1024, so its actual flight contains more segments.
Its first TCP retransmission timeout is at least 500 ms to accommodate NAN
availability gaps without repeatedly shrinking the send window.
Command and stalled-I/O limits are 10 seconds; each
phase has an overall duration + 30-second drain limit. Link loss or replacement,
malformed commands/frames and partial disconnects end the client while keeping
the listener available. Timed payload processing does not log each packet.

## Port details and scope

- The profile is a standalone publisher/responder with one active peer and one
  IPv6 link-local interface. STA auto-connect and STA IPv6 endpoints are omitted;
  concurrent infrastructure Wi-Fi, multiple active peers, initiator mode and
  video streaming are outside this implementation. Bulk TCP measurement is
  supported; radio scheduling and maximum-throughput tuning remain separate.
- NP SRAM grows from 96 to 160 KiB in both linker layouts. Both cores must use
  the same `NAN` option. Matching radio archives, task sizes and headers are
  selected only for this profile.
- NAN discovery uses both 2.4 and 5 GHz, matching the reference's dual-band
  profile. Console logs include NDP-to-TCP and first-SYN timings without
  packet-path printing.
- P-256 scalar multiplication uses Green2's ROM PKE API after three on-device
  comparisons with software mbedTLS, including random scalars and non-generator
  points. mbedTLS still validates private/public inputs and output points; key
  buffers are cleared. If the startup check returns an error, the software
  backend remains active. Green2's software fallback uses a larger ECP window
  and fixed-point precomputation; other profiles keep their smaller settings.
- IPC callbacks copy bounded events into a queue. A dedicated NAN task owns
  authentication, vendor commands and timers. NANDOW SET uses the 1448-byte ABI
  and does not wait for a GET-only reply.
- PASN uses SAE H2E group 19, CCMP-128 and a 32-byte KDK. The mbedTLS MPI adapter
  handles aliasing during modular exponentiation. Session reset frees hostap's
  handshake allocations and stale PMKSA entries. Pair verification requires a
  valid NIRA and an NPK bound to that exact NIK; lifetime zero means persistent.
- The vendor request event leaves `security` zero. `nan_wire.c` checks bounded
  NDP/NDPE attributes, the security bit, transaction token, NDP ID and NDI.
  Data paths also require a successfully authenticated peer NMI. Repeated
  requests for one transaction do not re-enter the response API.
- NP event forwarding works without enabling Linux SME mode. The selected
  upstream radio fixes cover PASN cipher selection, bootstrap discovery, FAW
  identifiers, NDL peer retention, immutable schedules, NDC control and restart
  discovery timers. Private-ABI patches refuse an unrecognized archive hash.
- Data travels over WHC interface 2 into FreeRTOS+TCP. Link-local addresses use
  the negotiated NDI and the explicit peer IID when present, otherwise EUI-64.
  Endpoint and neighbor-cache initialization run in the IP task.

## Native protocol tests

```sh
cmake -S component/amebagreen2/nan/tests -B build/amebagreen2/nan-asan \
    -G Ninja -DNAN_TEST_SANITIZERS=ON
cmake --build build/amebagreen2/nan-asan -j8
ctest --test-dir build/amebagreen2/nan-asan --output-on-failure
make -C project/amebagreen2 test
```

Tests cover SHA/HMAC, P-256, SAE H2E, a full PASN exchange with a hostap
initiator, tampered/duplicate Auth3, persistent identity binding, NIRA/NPK
verification after restart, malformed verification IEs and secure NDP parsing.
The TCP tests cover fragmented commands/frames, partial writes, both directions
on one stream, byte totals, maximum frame/ID/duration limits, truncated and
oversize frames, cancellation, slow clients and monotonic-clock wraparound.
ASan/UBSan and leak detection run on the native test executable; native tests
are build-time tools and are not needed on the board.

See [PROVENANCE.md](PROVENANCE.md) for vendored sources and licenses.

## Initial PING-only validation — 2026-11-16

Green2 on COM5, 4 MiB NOR, and a USB-connected iPhone 13 Pro running Flash
Studio. The phone confirmed initial PIN pairing. Pair Verify subsequently
loaded the saved NIK/NPK after firmware updates and board resets.

| Check | Result |
| --- | --- |
| Native crypto/PASN/NDP tests with ASan, UBSan and leak detection | 3/3 passed |
| Existing image, OTA flash and eFuse parsing tests (no OTP writes) | 12/12 passed |
| `NAN=ON` and separate `NAN=OFF` images | Both built |
| COM5 flash and readback of final firmware | Byte-for-byte PASS |
| First reconnect check after transaction deduplication | 5/5 PONGs |
| Extended zero-gap reconnect check | 18/20 PONGs; rounds 8 and 12 timed out |
| Final firmware, retained pairing after flashing | 5/5 PONGs |
| Final firmware after COM5 RTS reset, same saved pairing | 3/3 PONGs |
| `nan stop` then `nan start` | Publication resumed; saved pairing retained |

The extended run's successful exchanges had median 4.189 s and maximum
5.771 s. Both failures occurred during rapid reuse/teardown of the previous
phone data path; subsequent rounds recovered without resetting or re-pairing.
The exact cause of those two timeouts was not resolved in that build. This
failed run remains alongside the later throughput-profile results below.
Application report JSONs, including failures, are in
[tests/results/20261116](tests/results/20261116).

Original PING-only `firmware.bin`: 868384 bytes, SHA-256
`6843a244f1e5143727297868853e42053021dc36d5ffc5d87c5ab1da104a3190`.
This is the standalone NAN profile with heap diagnostics disabled. The
802848-byte combined application contains both cores. No Linux service was
used for discovery, authentication, storage, IPv6 or TCP.

Earlier diagnostic builds hit an AP heap fault during initial pairing and
a separate small-stack fault from packet-path `printf`. Full hostap session
cleanup fixes a reproducible native leak; packet-path logging was removed.
Neither fault recurred in the later validation, but the original initial-pairing
heap corruption was not isolated to a single proven cause. Long-duration and
repeated fresh-pairing qualification remain outside the completed checks.

## Throughput and connection latency update — 2026-11-16

The throughput profile adds bidirectional testing, dual-band discovery, larger
IPC buffers, NAN-specific TCP retry timing and ROM PKE acceleration. Saved
pairing remained valid through the updates. The startup PKE test passed all
three software/hardware comparisons; cached PASN Auth1 handling fell from
about 185 ms in the original software profile to about 22 ms.

The first throughput implementation measured 0.920 Mbps upload / 0.069 Mbps
download. After buffer and radio-profile changes, a ten-second run measured
9.440 / 1.050 Mbps, with matching sender/receiver byte totals. The rates below
use the receiver's timing; download timing includes draining queued data.

The PKE profile passed 5/5 and then 20/20 immediate reconnect checks. In the
20-round run, discovery-to-TCP-ready time was 0.469–3.211 s (median 0.551 s);
the connection stage after discovery was 0.115–2.834 s (median 0.171 s).
Most rounds reused the established NDP. Rounds 1 and 3 negotiated a new data
path and took 2.092 and 3.211 s total. A separate cold first connection took
3.952 s. These results establish fast warm reconnects, **not** a guarantee of
sub-second discovery or fresh NDP establishment.

On the exact final image, a **30-second-per-direction** test passed:

| Direction | Matching payload bytes at both ends | Receiver interval | Mbps |
| --- | --- | --- | --- |
| iPhone → Green2 | 32,505,856 | 30.046 s | 8.655 |
| Green2 → iPhone | 3,473,408 | 30.221 s | 0.919 |

Firmware RX/TX drop counters stayed zero and the send window stayed at 11,680
bytes. Download remains slower than the reference Linux host. This cold run
took 4.977 s to discover the service and 1.642 s more to connect (6.619 s total),
so discovery latency remains an open performance limitation. The complete
report is [throughput-final-30s.json](tests/results/20261116/throughput-final-30s.json).

After a COM5 RTS reset, the same saved pairing passed another ten-second test:
12,648,448 bytes upload / 1,638,400 bytes download, **10.011 / 1.276 Mbps** by
receiver timing. PKE startup cross-checks passed again. See
[throughput-after-reset-10s.json](tests/results/20261116/throughput-after-reset-10s.json).
After the 30-second run the AP had 178,404 bytes free; the `nan_tcp` stack
high-water check left 634 words (2,536 bytes). No protocol errors, software
RX/TX drops, assertions or task-stack faults were observed in these runs.

Native ASan/UBSan/leak checks pass 4/4, including the new TCP protocol test.
The separate `NAN=OFF` image also builds with the shared configuration changes.
The accelerated profile has been verified with saved-pairing authentication;
fresh PIN pairing was tested on the earlier software profile. No Linux
service participates in the on-board throughput or authentication path.

Image tested in this subsection: 899872 bytes, SHA-256
`d35d61bcd121314819ac0c5703ddc8aa242fc7e7b4b2e5cbba3715468dd8de63`.
The combined application is 834336 bytes. The 20-round report precedes only
a diagnostic-log wording change in this image.

## Download slowdown fixed — 2026-11-16

The roughly 1 Mbps download was caused by mismatched feature-disable glue,
not a limit demonstrated for this chip. The NAN profile selected the newer
radio archive but still compiled the older `wifi_feature_dis_wtn.c`. That
file lacked the disabled-R-Mesh `rtw_macid_wtn_mgt_init` stub, so the archive's
real R-Mesh initializer ran after NAN initialization and replaced the MACID
table capacity with `wtn_max_refugee_num + 2` (two slots in this profile).
NAN used peer IDs 10/11, and `wifi_hal_bb_sta_add` repeatedly rejected them
with `staadd fail`. The matching SDK stub preserves the NAN table. The port
now selects the matching complete WTN disable file for `NAN=ON` only; this
also supplies the updated queue hooks and IPC handler return types.

Timing evidence before the fix: FreeRTOS submitted the first eleven 1 KiB
TCP segments to WHC in 2 ms, while the iPhone received the same sequence over
48 ms. The slow stage was after WHC submission. In the bounded 256-event
download traces, submit-to-ACK median fell from 65 ms to 4 ms after the fix.
The phone capture changed from 20 TCP retransmissions to zero in the paired
ten-second runs; `staadd fail` disappeared. These traces sample the start of
each phase, rather than the entire session's RTT distribution.

| Ten-second run | Upload Mbps | Download Mbps |
| --- | --- | --- |
| Before the MACID fix, original release firmware | 9.161 | 1.055 |
| Matching WTN glue, diagnostic firmware | 9.670 | 13.281 |

Both endpoints agreed on all payload byte counts. Reports:
[before](tests/results/20261116/throughput-before-wtn-fix-10s.json) and
[after](tests/results/20261116/throughput-wtn-fix-diag-10s.json).
The TCP window, IPC buffer sizes, radio bands and App were unchanged between
these runs. Throughput still varies with NAN availability and RF conditions.

With diagnostics disabled, a 30-second-per-direction release test measured
**10.746 Mbps upload / 14.248 Mbps download**. Both ends counted 40,435,712
upload bytes and 53,477,376 download bytes; RX/TX queue drops remained zero.
The report is [throughput-wtn-fix-release-30s.json](tests/results/20261116/throughput-wtn-fix-release-30s.json).
Discovery plus connection took 3.496 s in this run; the download fix does not
establish a sub-second cold connection guarantee.

Release `firmware.bin`: **898080 bytes**, SHA-256
`a494d83d1a2401163ce33012885a135b1807ea988efaaab5a3437ab8005b0329`.
Combined application: 832544 bytes. COM5 programming and byte-for-byte readback
passed, saved pairing was retained, native ASan/UBSan tests passed 4/4, and the
separate `NAN=OFF` image built successfully. ELF inspection verifies that the
NP resolves `rtw_macid_wtn_mgt_init` to the SDK no-op stub.
An additional RTS reset passed startup/PKE checks and resumed publication.
The subsequent phone throughput launch was rejected because the iPhone was
locked, then unavailable on the Mac; no post-reset throughput result is claimed
for this release. Final idle AP free heap was 182652 bytes.

`NAN_NET_DIAG` defaults to OFF. When enabled, it records at most 256 TCP
header/timestamp entries per phase and counters for socket/WHC call time.
It allocates no packet-path memory and prints only from `nan_tcp` after the
phase. The release image contains neither its trace buffer nor its hooks.

## Bulk throughput follow-up — 2026-11-16

The phone returned online and the MACID-fixed release reproduced 9.962 Mbps
upload / 13.998 Mbps download after an RTS reset. The following sequential
tests used the same iPhone, App and saved pairing. Radio bands, country and
the 500 ms minimum retransmit timeout were kept unchanged. Each report checks
that both endpoints counted exactly the same application payload bytes.

| Profile (10 seconds per direction unless stated) | Upload Mbps | Download Mbps |
| --- | --- | --- |
| [Baseline, windows 8/8](tests/results/20261116/throughput-window-baseline-10s.json) | 9.962 | 13.998 |
| [Windows 16/16, AP IPC 32, TCP segment records 64](tests/results/20261116/throughput-window16-10s.json) | 12.085 | 15.477 |
| [RX aggregation 16, diagnostic sampling on](tests/results/20261116/throughput-rx-ampdu16-10s.json) | 12.422 | 16.440 |
| [ACK every second received packet, rejected experiment](tests/results/20261116/throughput-ack2-rejected-10s.json) | 11.403 | 16.030 |
| [Windows 24/24, AP IPC 40, TCP segment records 96](tests/results/20261116/throughput-window24-10s.json) | 12.667 | 18.878 |
| [TX/RX windows 32/16, stream buffers 40/24](tests/results/20261116/throughput-txwindow32-10s.json) | 11.738 | 19.532 |
| [Release, TX aggregation 32, diagnostics off, 30 seconds](tests/results/20261116/throughput-bulk-release-30s-1.json) | 12.735 | 21.520 |
| [Same release, second 30-second run](tests/results/20261116/throughput-bulk-release-30s-2.json) | 12.512 | 21.541 |
| [Task priority 8, rejected experiment, 10 seconds](tests/results/20261116/throughput-priority8-rejected-10s.json) | 8.236 | 16.720 |

Window and stream counts in the table use the configured MSS of 1460, whereas
the iPhone negotiates 1024-byte segments. The final socket reports a 59,040-byte
TX stream and 46,720-byte TX window. Enlarging both streams equally gave little
additional upload performance, so the final profile assigns more buffer space
to download while keeping the combined stream budget unchanged. Only NAN gets
the expanded segment/descriptor pools. NP buffers remain 32; RX aggregation is
16 and TX aggregation is 32. No application work buffer or task stack grew.

The ACK experiment removed a pending delayed ACK on the next incoming data
packet using a temporary linker wrapper around `prvSendData`. Although it
increased the ACK count, it did not improve throughput and was removed in full.
The final firmware uses the original FreeRTOS+TCP delayed-ACK implementation;
the submodule is unmodified. The independent configuration changes and RF
variation mean the table is not a controlled estimate of each change's gain.
Raising `nan_tcp` from priority 2 to 8 (above the WHC receive queue at 6, below
the IP task at 9) also failed to improve rates and was reverted. The rebuilt
priority-2 image reproduced the release hash below before being reflashed.

The release's first complete 30-second run transferred 47,906,816 upload bytes
and 80,740,352 download bytes, with matching counters, no protocol errors and
zero firmware RX/TX queue drops. The repeat transferred 47,054,848 upload bytes
and 80,805,888 download bytes, again with matching counters and zero queue
drops. AP free heap sampled during download was
37,420 bytes; `nan_tcp` retained 648 stack words. The `heap` command's
"min ever" value is only the current free count in this build because allocator
minimum tracking is disabled; it must not be cited as a true low-water mark.

Native ASan/UBSan tests pass 4/4 and the separate `NAN=OFF` image builds.
COM5 programming and byte-for-byte readback pass, and saved pairing survives
the update. The release contains no network trace buffer or ACK wrapper.
Bulk-tuning `firmware.bin`: **898080 bytes**, SHA-256
`b4ba784fae5618451857801a8975e9cbf0545f45c78732a473057cab5274ace5`.
Combined application: 832544 bytes.
After restoring and reflashing this exact image, another
[10-second check](tests/results/20261116/throughput-bulk-reflash-10s.json)
passed at **12.638 Mbps upload / 21.594 Mbps download**, with matching totals
15,925,248 / 27,000,832 bytes and zero firmware RX/TX drops. Saved-pairing
discovery plus connection took 3.980 s. The
[machine-readable summary](tests/results/20261116/bulk-tuning-summary.json)
records the final profile, image hash and remaining limitations.

Download now reaches the lower part of the user's Linux 20–30 Mbps comparison;
upload still falls short. This does not establish a radio or FreeRTOS ceiling.
One launch was rejected by iOS and another was cancelled before transfer;
after the user unlocked the phone, testing resumed. The release also had one
attempt remain in discovery without starting TCP; restarting the App succeeded
with 2.881 s to ready. Cold discovery/recovery latency is still variable and
is not resolved by enlarging TCP buffers.

The release's [20-round reconnect check](tests/results/20261116/iphone-bulk-release-twenty-rounds.json)
completed with **18 successes and 2 failures**. Rounds 1 and 6 discovered the
service but timed out before ready after 30 seconds; the following rounds
recovered without a board reset or new pairing. Successful ready times had
median 1.040 s, maximum 3.140 s. This is not a 20/20 stability pass. The
earlier PKE-only 20/20 result above belongs to a different firmware profile.

## Periodic heap logging — 2026-11-16

The current image adds the 10-second AP heap log described above. UART
verification recorded idle intervals of exactly 10.000 seconds and active
intervals of 10.021–10.039 seconds. Samples fell to 36,300 / 37,420 bytes
during upload/download and returned to 132,068 bytes after the socket closed.
The short transfer completed at 12.121 / 21.094 Mbps with matching endpoint
byte totals and zero firmware queue drops. See
[periodic-heap-log-check.json](tests/results/20261116/periodic-heap-log-check.json).

Current `firmware.bin`: **898176 bytes**, SHA-256
`734c034701b0afd80009320a0a620ab65a50b59e31b3e1c993937a2503880217`.
Combined application: 832640 bytes. Build and COM5 byte-for-byte readback pass;
saved pairing remains valid. The preceding bulk/reconnect qualification
belongs to the earlier image; this update only adds the periodic log.

## Module warning analysis — 2026-11-16

The [sanitized analysis](tests/results/20261116/module-warning-analysis.json)
compares existing UART captures with the pinned NP archive's call paths.
This analysis does not change the firmware or claim a new throughput result.
The two principal captures are the current heap-log image's 10-second runs
(12.121 / 21.094 Mbps) and the preceding release's second 30-second runs
(12.512 / 21.541 Mbps), with directions reported as phone upload/download.

| Message or activity | Evidence | Action supported by the evidence |
| --- | --- | --- |
| `rx_checking: nan_peer(...) does not exist!` | One occurrence in the current unsuccessful cold start; two in the partial reconnect capture, before any PONG and after four PONGs | Highest priority: handle an early PASN Auth1 when its peer context is not yet available; retain authentication and lifecycle validation |
| `peer bw=2 not support, force bw=0 ch=149/36` | 40 messages during the current 20-second transfer; 104 during the earlier 60-second transfer | Report once per peer/capability change and summarize repeats; do not force a wider bandwidth based on the message |
| `dw early: nanctrl_propagation` | Once in failed cold-start setup and once in successful final-release setup; absent from the two principal captures | Treat as a propagation-state diagnostic; it does not directly report a missed timer deadline |
| `FAW overlap, skip tx disc bcn` | Two in the current capture, both after download completed; none during the principal transfers | Keep discovery-beacon overlap avoidance; count skips if diagnosing discovery latency |
| FAW Add/Remove and NDL refresh | 13 refreshes, 78 adds and 78 removes in the 60-second transfer | Measure scheduling cost before changing it; most updates actually change the printed channel/time-slot tuples |

The missing-peer case has a concrete dropped-request path. All three warnings
follow `alg=7, seq=1, status=0`, then a peer-allocation log appears. In
`rtw_nan_data_psan.o`, `rtw_nan_pasn_rx_checking` calls `rtw_nan_peer_info_get`
at `+0x48`. A null result takes `+0x50..0x78`, prints the warning and returns
without reaching the PASN-start notification at `+0x1d6`.
`rtw_mgnt.o:rtw_on_auth` then returns without retrying this request. The
reconnect warning order is consistent with the App's failed rounds 1 and 6,
but the UART capture covers only the initial part of that 20-round test.
This confirms loss of those authentication attempts, not yet that a proposed
fix eliminates every 30-second timeout. A bounded early-frame/context handling
fix needs targeted tests and a new reconnect run; merely silencing the warning
would leave the behavior intact.

The bandwidth log originates in
`_rtw_nan_avail_attr_parse_channel_entry`, which compares the peer's advertised
width against `g_hal_spec` and updates the parsed entry. It is not evidence
of 104 active-link downgrades. Width 2 means 80 MHz and width 0 means 20 MHz;
the vendored configuration has `CONFIG_BW_MODE=0x00`. Both channels 149 and 36
are 5 GHz, independently of width. This does not establish the silicon's
maximum supported width. The `A` suffix is `RTK_LOG_ALWAYS`, level 1, so
selecting WARN level 3 does not remove these messages while retaining errors.
The 104 messages occupy approximately 5.1 KB, or **34 ms of wire time at
1.5 Mbps 8N1 over a 60-second transfer**. That estimate is not a measurement
of formatting, critical-section or scheduling cost, and does not establish
a throughput gain from suppressing them.

The 13 refreshes use two different six-entry FAW table shapes; only one
adjacent pair has identical printed tuples. Most refreshes therefore cannot
be removed as duplicate work. Even the identical pair requires checking
generation, absolute timing and peer/NDC state before skipping programming.
The `dw early` warning's immediate condition is a propagation flag, followed
by beacon start. The FAW-overlap message is emitted by the discovery-beacon
handler before stopping that beacon; it is not a TCP data-drop report.

Neither principal capture contains an ERROR/WARN-level line, `staadd fail`,
or a logged allocation failure/IPC timeout. The older pre-MACID-fix capture
has five `staadd fail` messages and belongs to a different, slow image.
Current software RX/TX queue drops are zero; sampled AP free heap is
36,300–37,420 bytes during transfer and 132,068 after socket close. Keep the
10-second heap log. These samples are neither a true minimum nor a proof
of zero radio loss or TCP retransmissions, and do not justify allocating
larger buffers without further measurement.

## Early Auth1 peer initialization — 2026-11-16

The FreeRTOS port now includes the reference SDK's `GREEN2_NAN_AUTH_PEER_FIX`
behavior. `fixes/whc_dev_nan_auth_peer_fix.c` wraps the NP's external
`rtw_nan_pasn_rx_checking` call. If an eligible Auth1 arrives before Subscribe
has created its peer, the wrapper allocates the normal unauthenticated vendor
context and calls the original handler. It never grants authentication,
installs a key or rewrites the received frame. Existing peers and subsequent
authentication transactions continue through the native handler. The native
allocator retains ownership of station capacity, timeouts and teardown.

Local guards reject null/out-of-range input and prevent allocation for a
source-address mismatch, multicast/zero address, fragments, protected/special
headers, other transactions/statuses or truncated IE framing. ASan/UBSan
tests cover those boundaries, missing/existing contexts, retries, allocation
failure and preservation of native rejection. All five native tests pass,
including the existing cryptographic, PASN, secure-NDP and TCP tests. The
linked NP disassembly confirms `rtw_on_auth` calls the wrapper and the wrapper
then reaches native lookup/allocation/PASN validation.

Firmware-only COM5 programming and byte-for-byte readback pass. Saved pairing
was used throughout the following tests; each module reset passed the PKE
self-test. The App was not modified.

| Scenario | Successful connections | Ready time, median / range |
| --- | --- | --- |
| [One connection after each module RTS reset](tests/results/20261116/iphone-auth-peer-cold-five-rounds.json) | 5/5 | 2.947 s / 2.938–4.334 s |
| [Continuous reconnect](tests/results/20261116/iphone-auth-peer-twenty-rounds.json) | 20/20 | 0.979 s / 0.461–3.234 s |
| [App restarts while the module continues running](tests/results/20261116/iphone-auth-peer-app-restart-ten-rounds.json) | 10/10 | 2.181 s / 1.878–2.521 s |

The [30-second-per-direction transfer](tests/results/20261116/throughput-auth-peer-30s.json)
completed at **12.177 Mbps upload / 21.033 Mbps download**, with matching
endpoint payload counts of 45,809,664 / 78,905,344 bytes and zero software
RX/TX queue-drop counters. AP free heap sampled at 36,300 bytes during upload
and 37,420 during download. Radio settings, bulk buffers/priorities and the
10-second heap log were retained; this change does not claim a throughput gain.

There were no missing-peer warnings, station-add failures or protocol aborts
in these captures. **The new early-peer allocation branch also never fired**:
every observed authentication already had a peer. Its missing-peer behavior
is covered by native tests, but these hardware runs do not demonstrate
recovery from the original race or prove that every connection-timeout cause
is fixed. A completed 100/100 reconnect report already existed on the phone
before this patch, so 35/35 here must not be presented as a measured reliability
improvement over the older 18/20 run. Cold readiness still takes several
seconds. Two earlier CoreDevice App-launch errors (one missing process ID,
one locked-device rejection) did not start a connection test and are excluded.

Current `firmware.bin`: **898432 bytes**, SHA-256
`c4317e78f61016ea61b138925faf354d968567b93cded17969f064145e6933bd`.
Combined application: 832896 bytes. See the
[validation summary](tests/results/20261116/auth-peer-fix-validation.json)
for exact image identity, test scope and remaining limitations.
