# Combo RF Wi-Fi / BT PAN traffic test — 2026-11-01

**Result: concurrent traffic is demonstrated, but combo is not yet stable.**
Concurrent TX completes; concurrent RX can time out the PAN TCP connection
and then lose the BT ACL link, including in a clean-boot PS-PTA test.

These measurements use **our application**, Bluedroid PANU and FreeRTOS+TCP,
with the pinned Bouffalo SDK `237ed937856bf435f4f3642ea4728294723e1d87`.
No lwIP or vendor Bluetooth host is linked into this firmware. The earlier
unmodified vendor demo comparison is recorded in [COEX_VALIDATION.md](COEX_VALIDATION.md).

## Build and board setup

```sh
make -C project/bl618dg_pan whole COEX=ON COEX_RF=combo \
  OUT=/tmp/bl618dg-combo-pan-build
```

Use COM15 for flashing/UART and COM23 for BOOT/reset. This build calls the
SDK's combo RF preparation and hardware coexistence implementation. It does
not enter standalone calibration. The GPIO16 external switch is held high
initially, matching the previously tested BT selection. `combo_ant bt|wifi|pta`
is an explicit board validation control; tests below record the actual choice.
The SDK is given its combo/shared-path recipe; this does not establish that
all physical SPDT-board operating modes are qualified.

The startup worker now uses priority 10, matching the vendor demo and our
Wi-Fi-only application. At priority 30, FHOST initialization did not finish;
at priority 10, Wi-Fi MGMR and Bluedroid startup complete. A dormant PAN endpoint
starts with a /32 mask so it cannot match every destination before DHCP.

Wi-Fi: 2.4 GHz, channel 12 / 2467 MHz, AP `Zeng`, BSSID
`DC:2C:6E:61:F4:C1`, board `192.168.100.7`, Windows peer `192.168.100.6`.
PAN: Windows NAP `20:0D:3D:9C:6D:81`, gateway/peer `192.168.138.1`;
the board's DHCP address changes after reconnection and is recorded in each log.
The AP/router configuration was not modified and credentials are not saved here.

## Traffic method

Run `tools/tcp_load_server.py --bind 0.0.0.0` temporarily on the Windows peer,
port 5001. The process is stopped after testing. Each board task binds its
interface's local IPv4 address and requires an on-subnet peer. Server JSON logs
also identify the board's source address, confirming the two separate routes.

```text
pan_connect 20:0D:3D:9C:6D:81
wifi_connect <ssid> <password> 2467
coex_status
pan_status

# Issue both commands immediately for concurrent traffic.
net_load pan0 192.168.138.1 5001 30 tx
net_load wl1 192.168.100.6 5001 30 tx
# Repeat using rx on both interfaces for downstream traffic.

# Optional native SDK power-save PTA, after the 2.4 GHz STA is connected.
coex_ps 1 50
coex_status
# Stop PS-PTA while keeping hardware coexistence with: coex_ps 0
```

`tx` is board-to-Windows, `rx` is Windows-to-board. TX throughput is taken from
the Windows receiver; RX throughput is taken from the board receiver. Rates
are payload bits / elapsed seconds, decimal Mbps. TCP send-accepted bytes may
exceed receiver bytes at close and are not treated as delivered traffic.
The server verifies the TX payload pattern. The tool retries temporary
FreeRTOS TCP `ENOSPC` backpressure and counts bounded stalls.

In parallel, Windows pings both current board IPs with DF and 1472 bytes of
ICMP payload (1500-byte IPv4 packets), usually 25 requests per interface.
These are ICMP loss measurements under TCP load, not UDP loss measurements.
The one shared radio and the small TCP windows mean these are integration
results, not maximum PHY throughput benchmarks or long-term reliability tests.

## Initial results and failure isolation

| Configuration / test | Wi-Fi Mbps | PAN Mbps | Observation |
|---|---:|---:|---|
| GPIO16 high, hardware PTA, Wi-Fi TX only, 10 s | 4.799 | — | Wi-Fi TCP baseline; receiver counted 6,035,640 bytes |
| GPIO16 high, hardware PTA, PAN TX only, 15 s | — | 0.373 | Wi-Fi remains associated; PAN receiver counted 703,720 bytes |
| GPIO16 high, hardware PTA, both TX, 30 s | 0.426 | 0.342 | Both completed; Wi-Fi ping 16/25, PAN 25/25 |
| GPIO16 high, hardware PTA, both RX requested | 4.322 | Not established | PAN TCP connect timed out; BT later disconnected with HCI reason 0x08 |
| GPIO16 high, PS-PTA 10 ms, both RX, 30 s | 0.147 | 0.407 | Both completed and remained connected; Wi-Fi ping 25/25, PAN 24/25 |
| GPIO16 low, PS-PTA 50 ms, both RX, 30 s | 0.904 | 0.142 | Both completed; Wi-Fi ping 25/25, PAN 17/25 |
| GPIO16 high, PS-PTA 50 ms, both RX requested | 1.226 | Not established | PAN TCP connect timed out; NAP subsequently unavailable; not a completed dual-flow run |

## Final clean-boot reproduction

The final image was reset, without reflashing or intermediate duty/pin changes.
GPIO16 stayed high. Wi-Fi was connected first, then `coex_ps 1 10` explicitly
disabled ordinary STA power saving before starting native PS-PTA. PAN was then
connected and obtained `192.168.138.191`. Status confirmed
`hardware=1 active=1 ps_pta=1 runtime=2 band=0 duty=10 ms`.

| Clean-boot test | Wi-Fi receiver Mbps | PAN receiver Mbps | Outcome |
|---|---:|---:|---|
| Both TX, 30 s | 0.176 | 0.285 | Both completed, no socket errors; host received 661,380 and 1,073,100 bytes respectively |
| Both RX requested, 30 s | 0.203 | Not established | PAN connect returned -116 (ETIMEDOUT); BT later disconnected, HCI reason 0x08; PAN IP went down |

During the clean TX run, both 1500-byte IPv4 pings passed 25/25. Wi-Fi RTT
was 19–208 ms (average 97), PAN 42–151 ms (average 91).

The continuous Windows monitor reported `On CLIENTS=1` at RX startup and
through 02:25:41; it changed to `InTransition` at 02:25:44 and `Off` at
02:25:53. The initial PAN TCP timeout occurred before that shutdown. Thus this
reproduction cannot be dismissed as an already-off Windows NAP. The evidence
does not yet separate RF/PTA starvation, controller/host scheduling and other
data-path causes. The subsequent off-NAP ping results are not valid PAN loss
measurements.

Repeated parameter/link changes also produced an interval with no UART reply,
including to a disconnect command. Reset restored normal startup. That symptom
has not been localized and is another reason not to claim stability.

The one successful high/10 ms RX run above proves that simultaneous receiving
is possible; its failure to repeat after a clean boot prevents treating it as
a stable workaround. Neither 5 GHz combo operation nor long-duration reliability
has been validated. The default build remains BT-only; combo is explicitly selected.

The high/10 ms and low/50 ms rows change two settings and do not isolate the
effect of either setting. AP-side Wi-Fi signal read -59 dBm at low and -60 dBm
after returning high; this small difference does not establish the physical
antenna path. All tested settings must be assessed using actual dual-link results.

Windows Bluetooth sharing was found off several times between
tests, including after board reset. An off NAP caused BNEP rejection; those
attempts are not treated as RF failures. The no-connections timeout was already
disabled, but sharing still needed to be restored. After PAN disconnection in
the hardware-PTA RX attempt, Windows routed subsequent PAN-address pings toward
its other gateway; those ping results are invalid as PAN radio loss metrics.
The HCI disconnect and TCP connect timeout remain recorded observations.

## Artifacts

Local build artifacts and full UART/host/ping logs are in
`build/validation/combo/` (ignored by Git). The configurable PS-PTA image is
`whole_flash_data.bin`, SHA256:
`80751f2c2b446fd283876f924548e121ab772fa7e83d894475ae33d0cf7f8482`.
The earlier retry-enabled image and ELF are retained under `firmware_ps10/`,
SHA256 `fe64cb020a8915e85df40be952fc0d556c695a72a293aca82efe70ee7a8ca780`.
The initial TX tool incorrectly stopped on ENOSPC; that preliminary attempt is
retained in the logs but excluded from the completed 30-second results above.

After testing, the temporary Windows TCP server was stopped and the private
AP credential file removed. The board remains on this combo image with GPIO16
high and PS-PTA 10 ms. Windows sharing and PAN were restored; the final observed
PAN address was `192.168.138.233`, with Wi-Fi still `192.168.100.7`.
