# SPP throughput validation — 2026-11-01

Four independent 30-second load runs completed, two per direction, plus two
10-second smoke runs. Every completed run had matching endpoint byte counts,
zero payload mismatches and zero application write errors. One additional
attempt stopped before sending payload because its UART START marker lost a
character; its log is retained and excluded from throughput measurements.

## Results

Receiving endpoint rates; Mbps and KB/s use decimal units.

| Direction | Run | Received bytes | Receiver Mbps |
|---|---:|---:|---:|
| Board → Windows | 1 | 2,607,660 | 0.689763 |
| Board → Windows | 2 | 2,998,710 | 0.790467 |
| Windows → board | 1 | 4,374,528 | 1.145727 |
| Windows → board | 2 | 4,374,528 | 1.145689 |

| Direction | Mean Mbps | Mean KB/s | Calculated UART 8N1 equivalent |
|---|---:|---:|---:|
| Board → Windows | 0.740115 | 92.51 | 925144 baud |
| Windows → board | 1.145708 | 143.21 | 1432135 baud |

The earlier PAN/TCP means were 0.564191 / 0.739495 Mbps (board TX/RX).
SPP is approximately 31% / 55% higher in these separate bench sessions.
The 10-second TX smoke result, 0.980054 Mbps, is excluded from sustained rates.
The old RX smoke image lacked receive timestamps: its sender rate is not
reported as receiver throughput.

## Setup and measurement

- BL618DG Classic controller + Bluedroid. Wi-Fi and PAN/TCP are not initialized.
- Shared antenna switch GPIO16 high.
- SDP Serial Port service UUID 0x1101, SPP 1.2, RFCOMM channel 1, requested MTU 990.
- Board B4:E8:42:3C:B5:2B; Windows 20:0D:3D:9C:6D:81, Qualcomm FastConnect 7800 Dual Bluetooth Adapter. Existing bond reused.
- COM15 at 2 Mbaud carries shell/logs, COM23 controls reset. Payload uses a native Windows RFCOMM socket connecting by standard service UUID through SDP, without a virtual COM port.
- Stack calls run on BTU. The benchmark uses native Bluedroid SDP/RFCOMM APIs without the ESP VFS/SPP API wrapper.
- Pattern byte[n] = n modulo 251; stream offsets are retained across partial writes. Final counters must match and receiver pattern checks must pass.
- Board TX: Windows received bytes divided by elapsed time from START acknowledgement to last payload, including queue drain. Board queued bytes must equal Windows received bytes.
- Board RX: board received bytes divided by first-to-last receive callback time. Windows sends for about 30 seconds, then allows two seconds for queued data before stopping the counter.
- RX spans were 30.545 / 30.546 seconds. Sender-only rates, 1.166117 / 1.163101 Mbps, are not substituted for receiver rates.

## Reproduction

```sh
make -C project/bl618dg_pan whole SPP=ON OUT=/tmp/bl618dg-spp-build
```

Flash the resulting whole image with `tools/flash.ps1`, preserving PSM bonds.
Then run on Windows from the project directory:

```powershell
.\tools\spp_load.ps1 -Mode tx -Seconds 30 -Reset -Log .\build\validation\spp\tx-1.json
.\tools\spp_load.ps1 -Mode rx -Seconds 30 -Reset -Log .\build\validation\spp\rx-1.json
```

Repeat with different filenames, then summarize on Linux:

```sh
python3 project/bl618dg_pan/tools/summarize_spp.py
```

Shell commands are `spp_load tx|rx seconds` and `spp_load stop`. Reset before
independent connections. Sockets and COM ports close after every run.
The board is left on the SPP validation image, ready for another connection.

## Validation and limits

- COM15 flash verification succeeded. Image SHA256: `14756cfcbfc6a8c128b3fa9d1fcff02a0f2e42d65bc5194c6c7c5e0eefc252fc`.
- SDK `237ed937856bf435f4f3642ea4728294723e1d87`, with the pre-existing FreeRTOS+TCP adapter changes.
- Standalone SPP and default PAN builds passed; existing upstream compiler warnings remain.
- No differing endpoint byte counts, payload mismatches or link loss occurred within the four completed long runs.
- TX short/zero writes are retried as backpressure and never counted as sent bytes. The long TX runs recorded 12,803 / 12,223 such scheduling turns.
- Upstream L2CAP emits `l2cab is_cong_cback_context` diagnostics during TX. Source inspection identifies a recursion guard that defers scheduling until controller completions. Logs are retained; this is not a log-clean stack qualification.
- Each long run logged two HCI `continuation for unknown packet` messages around connection setup. SPP subsequently connected and payload checks passed; these setup diagnostics remain unresolved.
- These are separate-direction measurements, not simultaneous bidirectional throughput, radio PHY limits or physical UART/RTS/CTS qualification. The 0.690–0.790 Mbps TX range does not establish sustained 921600-baud 8N1 board-to-host capability.
- Generic SPP is not an ordinary iOS App data socket. Bluetooth Classic accessory integration needs separate [Apple External Accessory/MFi evaluation](https://developer.apple.com/accessories/).

Local raw artifacts are under `build/validation/spp/`: manifest, flash/boot
logs, per-run JSON, decoded UART logs, summary.json, and the failed ACK log.
Build and raw artifacts are ignored by Git.
