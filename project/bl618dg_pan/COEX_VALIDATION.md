# Latest SDK coexistence integration — 2026-11-01

SDK: official Bouffalo `237ed937856bf435f4f3642ea4728294723e1d87`.
Host: ESP-IDF Bluedroid PANU; IP stack: FreeRTOS+TCP. No lwIP is linked.

Follow-up: the user's requested **combo** traffic tests now run in our
application with `COEX=ON COEX_RF=combo`. See [COMBO_VALIDATION.md](COMBO_VALIDATION.md)
for receiver throughput, packet loss, PS-PTA comparisons and remaining failures.
The standalone calibration blocker described below remains unresolved.

## Implementation

- `pan_coex.c` declares standalone single-antenna/SPDT wiring on GPIO16, then
  calls `wifi_mgmr_coex_rf_prepare()`. GPIO16 high selects BT, matching the BSP's
  even-GPIO polarity. The upstream BSP owns fixed/dynamic SPDT transitions.
  The user's board diagram confirms separate BT2.4/Wi-Fi2.4 outputs into this
  switch, with Wi-Fi5G bypassing it toward the dual-band antenna. Thus this
  topology declaration matches the supplied diagram.
- Wi-Fi FHOST/MACSW, RF calibration, PTA recipes and optional PS-PTA remain the
  upstream implementations. `component/bouffalo_wifi_tcp_config` provides the
  FHOST network abstraction and WPA address conversions using FreeRTOS+TCP.
- AP-only radio startup, ROMAPI disabled, 32 FreeRTOS priorities and a larger
  timer task stack match the official coexistence example. The unused NP image
  may still be packed, but is not started.
- Preserve `LFS_KV_NAMESPACE="/KV"` from the previous at-project SDK fork, so
  upgrading to upstream does not hide persistent bonding records.
- `pan_http` binds its socket to the PAN endpoint; `wifi_http` binds to `wl1`.
  This avoids accidentally treating Wi-Fi-routed HTTP as a PAN test.

## Latest SDK BT-only regression

The default image was flashed through COM15/COM23 and verified. With the
original `/KV` namespace restored, existing Windows bonding was reused; BNEP
and DHCP succeeded (`192.168.138.151`). PAN-bound HTTP returned
`HTTP/1.1 200 OK`. A 1472-byte ICMP payload (1500-byte IPv4 packet) passed 3/3,
73–114 ms, average 91 ms. This validates the SDK upgrade's BT-only route and
does not validate concurrent traffic. Windows Bluetooth sharing was restored
using its real public adapter, and its no-connections auto-shutdown was disabled
after repeated hotspot shutdowns interrupted the tests. `windows_share.ps1
-IdleTimeout Enable` restores that setting. The router configuration was left
unchanged.

## Actual blocker

The attached board completes combo/Wi-Fi RF calibration (`rfparam_init` returns
0), selects standalone mode, then stalls during the second RF initialization,
before FHOST, the TCP adapter or the Bluetooth host starts.

Tick-PC sampling identified vendor PHY function
`rf_pri_bz_only_lo_fcal_meas`. It triggers measurement through `0x200040b4`,
waits for bit 0 of `0x200040b8`, and reads the frequency count from bits 4–19.
The initial capture read only zero; subsequent isolated tests also briefly
captured `0x00000001` (done set, count zero). Therefore the evidence does not
establish that the done bit never asserts. The calibration does not return,
and repeated samples locate execution in measurement polling or its delay.
The MAC timer at `0x24b00120` kept advancing, so this is
not a stopped FreeRTOS tick or a hung scheduler. Disabling NP and ROMAPI did
not resolve it. The user performed a full power removal/reconnect; cold boot
reproduced the same state. The SDK's B0 and B0_BZ PHY archives contain identical
objects (17/17), so that alternate archive does not offer a distinct fix.

No calibration-complete bit was forced, no calibration step was skipped, and
no arbitrary RF register recipe was substituted. The SDK PHY/BSP sources are
unchanged; optional application-level diagnostics are isolated by `RF_DIAG=ON`.
Resolving this
requires identifying the standalone PLL's missing hardware/calibration
condition or obtaining a matching PHY fix; the captured state alone does not
prove whether the root cause is board hardware or PHY initialization.

## RF isolation follow-up

An isolated application keeps the shell and timer running while a lower-priority
worker performs one calibration attempt per reset. It starts no Bluetooth host,
Wi-Fi association or IP traffic. The controller is linked for the optional
controller cases and is initialized only when selected.

| `rf_test` case | Sequence | Observed result |
|---|---|---|
| `direct` | Standalone full calibration directly | Does not return |
| `combo` | Combo full calibration, then standalone full calibration | Original BSP sequence stalls at standalone |
| `mac` | Combo, idle Wi-Fi MAC, standalone | Does not return |
| `power` | Combo, standalone domain enabled through vendor API, 50 ms settle, standalone | Does not return |
| `controller` | Combo, idle BT controller, standalone | Does not return |
| `both` | Combo, idle Wi-Fi MAC and BT controller, standalone | Does not return |
| `bz` | Combo, then standalone `wl_init` in BZ mode with full calibration and parameter loading enabled | Does not return |

The extra startup orders do not remove the failure. In particular, BZ-only
full calibration also fails, without repeating WLAN calibration. This narrows
the investigation to standalone PHY initialization/calibration conditions;
it does not identify a hardware defect or prove a particular PHY fix.

Read-only RF LDO register `0x2008f880` was `0x70020102`, including the LDO
power-enable bit set. Calling `wl_standalone_rf_set_status(1)` early did not
help. The public eFuse information API reported package 1 (QFN56), version 0,
4 MB flash and no integrated PSRAM. No authoritative eFuse-version-to-A0/B0
mapping was established, so this value alone does not justify switching PHY
libraries. The selected SDK B0 PHY reports its version string as `V0`.

Logs and the diagnostic ELF/image are saved locally under
`build/validation/rf_order/` (ignored build artifacts). Older case logs use
earlier diagnostic builds, so their PCs must not be resolved against the final
ELF. The final BZ test image SHA256 is
`0467394017f344b521e37c5c9b21af52d669b7eda7e34c74ec21d73c839c648f`.
After diagnosis, the board was restored through COM15/COM23 to the previously
validated Wi-Fi-only image. It rejoined the 5 GHz AP on channel 48, obtained
`192.168.100.7`, and `wifi_http example.com` returned `HTTP/1.1 200 OK`.

## Unmodified vendor demo comparison

At the user's request, a clean copy of the pinned official SDK commit was
extracted with `git archive` into `/tmp/bl618dg-stock-sdk`. All tracked file
contents were checked against that commit after compilation: zero differences.
Neither of the working SDK's FreeRTOS+TCP integration patches was included.
The demo used its native lwIP and Bluetooth host, with no Bluedroid, PAN port,
RF diagnostic hooks or application source changes. This use of lwIP is confined
to the vendor comparison, not the PAN project's networking implementation.

Build command in `examples/coex/wifi_bz_coexm`:

```sh
env PATH=/home/zxf10/at-project/platform/toolchain_gcc_zephyr_linux/bin:$PATH \
  make CHIP=bl618dg BOARD=bl618dgdk CPU_ID=ap CPU_MODEL=b0
```

The vendor defaults disable NP startup and ROMAPI. Build, stock image packing,
and flashing through COM15/COM23 succeeded. Regional erase preserved PSM.
The whole-image SHA256 verified by the flasher was
`c7a4a8a48bbc245b7d3e84a69724082b66627f1a7104d046b6663a4a651bfb3b`.

| Test | Commands after reset | Result |
|---|---|---|
| Actual board wiring | `wifi_coex_board_config standalone_single_ant 16`, then `wifi_coex_init standalone` | First combo calibration prints `modem_init done`; switches to standalone; second RF initialization never reports completion during observation. A subsequent CLI query produced no response during another 15-second capture. |
| Internal combo startup control | `wifi_coex_board_config combo`, `wifi_coex_init combo`, `wifi_bt_init` | RF reports `coex RF prepared: combo`; Wi-Fi reaches `CODE_WIFI_ON_MGMR_DONE`; Bluetooth callback prints address `B4:E8:42:3C:B5:2B`. |

The combo status was `hardware_configured=1 active=0 runtime=hardware_only
ps_pta_running=0 band=none`. No Wi-Fi association, Bluetooth connection or
concurrent traffic was attempted in this control. Combo is not the declared
physical topology of this board and its successful internal startup does not
validate an antenna route or provide a production workaround.

The unmodified demo reproduces the standalone RF preparation failure, without
the project's host/network integration. It provides an independent reproduction
for PHY/BSP investigation; the exact failing instruction was not instrumented
in this stock build, so the earlier measurement-PC evidence remains from the
separate diagnostic image.

Firmware, ELF, resolved configuration, provenance and logs are saved locally
under `build/validation/stock_demo/`. The board is now left on the stock demo,
reset with `standalone_single_ant 16` declared and RF initialization not yet
requested, so its shell remains available.

**Standalone concurrent Wi-Fi/PAN startup has not passed validation.** FHOST and the network adapter execute in the Wi-Fi-only and combo builds;
see [Wi-Fi-only validation](WIFI_VALIDATION.md). They have not executed as part
of a successful standalone concurrent PAN/Wi-Fi startup.
Standalone 2.4 GHz dynamic SPDT/PTA, PS-PTA and 5 GHz coexistence remain unverified. The router
was inspected over the user-provided SSH access; its configuration was not
modified, and credentials are not committed to the project.

## Reproduce and recover

```sh
# Default: latest SDK, working BT-only route, GPIO16 fixed high.
make -C project/bl618dg_pan whole

# Explicit experimental coexistence build in a separate directory.
make -C project/bl618dg_pan whole COEX=ON OUT=/tmp/bl618dg-coex-build

# Optional isolated reproduction, shell remains available during calibration.
make -C project/bl618dg_pan whole RF_DIAG=ON OUT=/tmp/bl618dg-rf-order-build
```

After flashing the RF diagnostic image, run `rf_test bz` (or a case in the
table) through `tools/console.ps1`. Reset between cases with `-Reset`; do not
run another case after a partial calibration in the same boot.

Flash the selected image with `tools/flash.ps1 -Fw <Windows-accessible-path>`.
The coexistence image may stall before shell commands can run. To recover,
flash the default BT-only image using COM15/COM23. Regional erase preserves PSM.
The SDK net_al integration patch is checked/applied by the project's Makefile.

After RF preparation is repaired, the pending validation sequence is:
`coex_status`, PAN connect/DHCP, `wifi_scan`, STA association on each band,
`wifi_http` and `pan_http` while both links are connected, 1500-byte ping,
then optional `coex_ps 1/0` on a connected 2.4 GHz STA with ordinary PS off.
Do not interpret a configured duty value as evidence that PS-PTA is running.
