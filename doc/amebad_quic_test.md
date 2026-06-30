# AmebaD (RTL8721D) QUIC LAN speed test — ngtcp2 + wolfSSL→picotls/mbedTLS TLS 1.3

A QUIC v1 client on KM4 built on **ngtcp2** (transport) + the in-tree **wolfSSL**
expanded to a full **TLS 1.3 + QUIC** crypto layer, driven over the existing
**FreeRTOS-Plus-TCP** UDP socket bring-up. It runs the quic-interop **"perf"**
protocol against a host QUIC server and measures bidirectional LAN throughput.

> **Status (2026-06-09): ✅ end-to-end working — bidirectional throughput measured
> on hardware.** Boots via `QUIC=ON TFM=ON` (QUIC image runs as the TF-M NSPE;
> standalone QUIC hit a boot-time SecureFault — see "Boot: why TF-M" below).
>
> | Direction | Transfer | Throughput |
> |---|---|---|
> | **Download** (server → chip RX) | 4 MB / 7.40 s | **~4.53 Mbps** |
> | **Upload** (chip TX → server) | 4 MB / 13.88 s | **~2.42 Mbps** |
>
> Stable and repeatable across rounds; both sides report a clean `[ok]` teardown.

> **Update (2026-06-14): wolfSSL → ngtcp2 + picotls/mbedTLS migration — measured, regressed.**
> Same chip, same host (native Windows C# MsQuic/Schannel server `192.168.4.250:4567`,
> **AES-128-GCM**), same 4 MB/direction perf protocol — only the chip's TLS 1.3 crypto
> changed (wolfSSL → picotls on the mbedTLS/PSA backend). Throughput **dropped**:
>
> | Direction | wolfSSL (asm GCM) | picotls/mbedTLS (SW C GCM) | regression |
> |---|---:|---:|---:|
> | **Download** (RX) | ~4.53 Mbps | **~1.72 Mbps** (1721–1739 kbps) | **2.6× slower** |
> | **Upload** (TX)   | ~2.42 Mbps | **~1.29 Mbps** (1283–1292 kbps) | **1.9× slower** |
>
> **Root cause = AES-128-GCM**, the QUIC record-layer cipher (Schannel offers only AES-GCM).
> wolfSSL does GCM with Thumb2 asm (~2 MB/s); **mbedTLS does it in portable C at ~419 KB/s**
> (`[MCB]` AES-128-GCM = 465 cyc/B — see [amebad_crypto_mbedtls_vs_wolfssl.md](amebad_crypto_mbedtls_vs_wolfssl.md)).
> Per-byte GCM is the wall, so the QUIC throughput regression tracks the GCM crypto regression.
> **To recover**: a Cortex-M AES-GCM for mbedTLS (asm / PSA AES driver / AES tables in SRAM), or a
> ChaCha20-capable peer (mbedTLS ChaCha20-Poly1305 = 79.7 cyc/B ≈ 2.45 MB/s, ~5.8× faster than its
> GCM — but the C# Schannel host can't negotiate ChaCha; would need a native ChaCha-capable QUIC server).
> Repro: `make QUIC=ON TFM=ON`; numbers stable across rounds, clean `[ok]` teardown.

> **Update (2026-06-14): HW AES-GCM offload — regression recovered AND past the wolfSSL baseline.**
> `make QUIC=ON TFM=ON HWGCM=ON` routes mbedTLS GCM to the RTL8721D IPSEC engine
> (`MBEDTLS_GCM_ALT`, NSPE-direct, ~48 MB/s vs 419 KB/s software — see
> [amebad_crypto_mbedtls_vs_wolfssl.md](amebad_crypto_mbedtls_vs_wolfssl.md) /
> reference_amebad_hw_crypto). Same host/cipher/protocol. Result:
>
> | Direction | wolfSSL (asm GCM) | mbedTLS SW GCM | **mbedTLS + HW GCM** |
> |---|---:|---:|---:|
> | **Download** (RX) | ~4.53 Mbps | ~1.72 Mbps | **~6.8 Mbps** (6778–6813 kbps) |
> | **Upload** (TX)   | ~2.42 Mbps | ~1.29 Mbps | **~2.95 Mbps** (2949–2950 kbps) |
>
> Download ~4× the software regression and **~1.5× over the wolfSSL baseline**, approaching the
> iperf WiFi ceiling (~9 Mbps): **crypto is no longer the QUIC bottleneck — it's WiFi/protocol-bound.**
> Stable across rounds. (Upload gains less — bounded more by the single-task send path / ngtcp2
> pacing than by crypto.) HWGCM=ON requires TFM=ON (NS-direct needs the SAU; the Makefile enforces it).

## Results & performance analysis (2026-06-09)

**Topology.** chip on WiFi `Zeng` (DHCP `192.168.4.244`) ↔ Windows host
`192.168.4.250` on the wired `vEthernet (External-Bridge)` segment, same
192.168.4.0/24 (MikroTik hAP ac², no client isolation). 4 MB per direction,
perf protocol (ALPN `perf`: client opens a bidi stream, sends 8-byte big-endian
download length + upload bytes + FIN; server returns that many bytes).

**Why QUIC (4.5 / 2.4 Mbps) is slower than plain TCP (iperf ~9 Mbps on the same
link).** iperf's number is the *plaintext* WiFi ceiling, with send/recv batched in
the IP task / driver. QUIC stacks three costs on top, all run **single-threaded in
one `quic_cli` task** (no DMA/multicore pipelining):

1. **Per-byte AES-128-GCM** — software, Thumb2 asm (M33 has no AES instructions):
   measured ceiling **~16.6 Mbps** (2083 KB/s, see `amebad_wolfssl_hwaccel_eval.md`).
2. **Header protection** — one extra AES-ECB block per packet.
3. **Userspace protocol** — ngtcp2 framing/ACK/loss-detection in the app task, plus
   a `sendto`/`recvfrom` syscall per UDP datagram (TCP batches in the driver).

These are **serial**: receive → decrypt → ngtcp2 → encrypt+send ACK, no overlap.
Ideal ≈ 1/(1/9_WiFi + 1/16.6_crypto) ≈ 5.8 Mbps; minus single-thread protocol
overhead → **4.5 Mbps** measured. Matches.

**Why upload (2.4) < download (4.5).** The sender side is heavier: encrypt every
full packet + header protection + maintain the retransmit buffer + decrypt inbound
ACKs + congestion/pacing + the WiFi TX submit path. The receiver mostly decrypts +
advances the window and emits small ACKs. Confirmed **CPU/protocol-bound, not
window-bound**: raising the upload in-flight cap 32K→96K gave no speed-up (and
introduced instability), and at sub-10 ms LAN RTT a 32K window already permits tens
of Mbps — far above 2.4. So crypto/protocol CPU is the wall, not pacing.

**Could it be faster?** ChaCha20-Poly1305 is ~2.5× faster than AES-GCM on this core
(portable C ~5 MB/s vs AES-GCM asm ~2 MB/s). But the Windows host can't be the peer
for it: Schannel's TLS 1.3 only offers `TLS_AES_128/256_GCM` (no
`TLS_CHACHA20_POLY1305_SHA256`), and C# `System.Net.Quic` is MsQuic+Schannel. Testing
ChaCha end-to-end would need a ChaCha-capable native Windows QUIC server (cross-compile
the wolfSSL/OpenSSL `quic_perf` with mingw).

**UDP checksum is NOT the bottleneck (2026-06-13).** Disabling the software UDP TX
checksum on the QUIC socket (`FreeRTOS_setsockopt(sock, 0, FREERTOS_SO_UDPCKSUM_OUT,
NULL, 0)` — `quic_test.c`; outgoing UDP goes out with checksum 0, IP header checksum
still computed) gave **4.20/1.89 Mbps vs 4.19/1.88** with it on — no measurable change.
QUIC's AEAD makes the UDP checksum redundant anyway. Consistent with the CPU/protocol-
bound finding: the per-datagram software checksum pass is noise next to the crypto.
(checksum-0 UDP traverses the WiFi→host path fine; the native-Windows server still
received and completed both transfers.)

## Build

```
cd project/amebad && make QUIC=ON TFM=ON      # -> build/amebad/Image_All.bin (~907 KB)
cp build/amebad/Image_All.bin project/amebad/out/Image_All.bin   # flash.ps1 reads out/
```

`QUIC=ON` sets `-DAMEBAD_ENABLE_QUIC=ON` (forces `AMEBAD_WOLFSSL_TLS=ON` → full
TLS 1.3 + QUIC wolfSSL; builds ngtcp2; compiles `app_km4/quic_test.c` and runs it
from the network-up hook instead of iperf). `WOLFASM` defaults `ON` (record-layer
AES/SHA via Thumb2 asm). `TFM=ON` builds the in-tree TF-M secure world and makes
app_km4 the NS side — **required to boot** (see below).

QUIC client target is at the top of `project/amebad/app_km4/quic_test.c`:
`QUIC_SERVER_*` = `192.168.4.250:4567`, ALPN `perf`, 4 MB/direction.

### Boot: why TF-M

A standalone `make QUIC=ON` image **does not boot** — KM4 takes a **SecureFault /
AUVIOL** during early boot (`SOCPS_InitSYSIRQ_HP` enabling the SYS IRQ makes a
Secure-attributed access the smaller NS images don't; JTAG evidence preserved in git
history of this file). Running the QUIC image as the **TF-M NSPE** (`TFM=ON`) supplies
the secure-side setup that was missing, and it boots: TF-M SPE (PSA PING PASS) →
FreeRTOS V10.6.2 → WiFi → DHCP → QUIC. JTAG also disproved the earlier "TF-M breaks
WiFi TX" theory — a clean TF-M iperf run streams ~9 Mbps.

## Host QUIC server — native Windows C# (System.Net.Quic)

The server must be **native Windows**, not WSL: WSL2 mirrored mode delivers inbound
TCP to WSL listeners but **not inbound UDP**, so a WSL QUIC server never sees the
chip's packets. Source: `project/amebad/quic_perf/win/{Program.cs,quicperf.csproj}`
(perf protocol, ALPN `perf`). Built at `C:\Users\zxf10\quicperf_win\`.

```
# dotnet is user-local, not on PATH; the .exe apphost can't find the runtime, so run the dll:
C:\Users\zxf10\.dotnet\dotnet.exe \
  C:\Users\zxf10\quicperf_win\bin\Release\net8.0\quicperf_win.dll  192.168.4.250 4567
```

- **Bind the explicit IPv4** (`192.168.4.250`), not `0.0.0.0`: MsQuic otherwise
  opens a `::` socket that did **not** receive the chip's IPv4 datagrams here.
  Binding the IPv4 makes `conn from 192.168.4.244:...` appear immediately.
- **Port 4567**, not 4433: WSL mirrored permanently reserves any port WSL ever bound
  (4433), so the native Windows bind fails with 10048.
- C# gotchas: net8 QUIC needs `<EnablePreviewFeatures>true`; Schannel cert must be
  imported `Exportable|PersistKeySet|UserKeySet` (NOT `EphemeralKeySet` →
  `QUIC_STATUS_CERT_NO_CERT`). Server logs `conn from` / `DOWNLOAD`/`UPLOAD .. Mbps`
  to `C:\Users\zxf10\quicperf_win\srv.log` (even on abort).

## Flash + console

- Flash: `project/amebad/flash.ps1 -BootPort COM14 -FlashPort COM3` (boot=COM14
  drives CHIP_EN; flash+log=COM3 @ 1,500,000). Board re-enumerates — COM10/25 are
  gone; COM3 = CH340 console@1.5M, COM14 = FTDI CHIP_EN.
- Console COM3 @ 1.5 Mbaud. Reset into app: COM14 RTS pulse (keep DTR low/off).
  Helper scripts: `/mnt/c/temp/reset_cap.ps1` (reset + capture), `read_only.ps1`.
- The client loops rounds and prints a 1 Hz `[QUIC] .hb so/rx/ack/sr/fin` heartbeat
  plus a per-round `DOWNLOAD/UPLOAD ... kbps (~N Mbps)` summary.

## Key fixes that made the transfer work (2026-06-09)

These are the bugs found going from "handshake-only" to "completes a 4 MB transfer":

1. **★ Heap exhaustion (the hang) ★** — download froze at exactly **196608 B
   (192 KB)**; JTAG caught `PC = vApplicationMallocFailedHook` (TCB `quic_cli`).
   KM4 heap is 250 KB but the QUIC transport params advertised a **512 KB stream /
   1 MB connection** flow-control window; packet loss (40-buffer RX pool overrun
   under the flood) made ngtcp2 buffer reordered data up to that window → heap died.
   **Fix:** `QUIC_FLOW_WINDOW` 512K → **48K** (LAN BDP is a few KB; small window
   costs no throughput). Also added an upload **in-flight cap** (`QUIC_UPLOAD_INFLIGHT_CAP`
   = 32K, paced by written-acked) so the retransmit buffer can't blow the heap when
   the LAN cwnd opens up.
2. **RX drain** — the loop now drains *every* queued datagram (non-blocking recvfrom
   loop) before flushing, so a download flood can't outrun it and exhaust the RX pool.
3. **`sendto` non-fatal** — a transiently-full TX path (EWOULDBLOCK) no longer tears
   the connection down; QUIC retransmits.
4. **WiFi association** — dual-band `Zeng` band-steers STAs off 5 GHz with assoc
   status 30; `scan_print_handler` now prefers 2.4 GHz (ch ≤ 14), then RSSI, and
   `wifi_connect` is wrapped in an **8-try retry loop** (re-arming pscan each attempt).
   *Note (2026-06-09): even on 2.4 GHz the **first** assoc usually gets `status 30`
   and attempt 2 succeeds — root-caused to the AP keeping a **stale station entry**:
   the driver-only chip doesn't send a deauth on reset, so on re-connect the AP rejects
   the duplicate-MAC assoc once (30) until it clears the old entry. Manually removing
   the chip's `registration-table` entry on the AP → first assoc succeeds immediately,
   confirming it. **Fixed (2026-06-09): `rltk_wlan_tx_deauth(1, 0)` before each assoc.**
   The `CONFIG_IEEE80211W` block in `osdep/wlan_intf.h` exposes a raw broadcast-deauth
   (`rltk_wlan_tx_deauth`, in lib_wlan.a). Sending it on cold boot (no BSSID context
   needed — the broadcast form works) makes the AP drop the stale entry (`Recv Disassoc
   status_code=8`), so attempt 1 associates first try — verified 3/3, no more status 30.
   `wifi_disconnect()` would NOT work here (no-op with no live connection); the broadcast
   `rltk_wlan_tx_deauth` does. The 8-try retry loop stays as a backstop for genuine
   transient rejects.*
5. **C# server IPv4 bind** + **port 4567** (see host server section).

## Earlier wolfSSL/ngtcp2 integration fixes

- **key-share group** — wolfSSL default `preferredGroup[0]` = secp521r1, but the chip
  compiles only P-256 + X25519 → ClientHello key_share `BAD_FUNC_ARG(-173)`. Fix:
  `wolfSSL_CTX_set_groups(ctx, {WOLFSSL_ECC_X25519, WOLFSSL_ECC_SECP256R1}, 2)`.
- **XREALLOC** — TLS 1.3 handshake really reallocs; the chip's `wolfcrypt_glue.c`
  XMALLOC/XFREE/XREALLOC are size-prefixed (8-byte header, 8-byte aligned).
- ALPN uses `WOLFSSL_ALPN_FAILED_ON_MISMATCH` (not `WOLFSSL_ALPN_MATCH`=1 → BAD_FUNC_ARG).
- `DiagPrintf` does **not** support `%u` (prints a literal "u") — use `%d`.

## Integration gotchas (transport/crypto glue)

- **ngtcp2** (`component/amebad/ngtcp2/`): `NGTCP2_USE_GENERIC_SOCKADDR` +
  `NGTCP2_AF_INET/6`; `htonl/ntohl` in `ngtcp2_port.c`; size-prefixed `pvPortMalloc`
  shim; empty `<wolfssl/options.h>` shim; hand-written `generated/ngtcp2/version.h`.
- **wolfSSL TLS/QUIC** (gate `AMEBAD_WOLFSSL_TLS`): `WOLFSSL_NO_SOCK`; stub
  `WOLFSSL_IP4/6` + `XINET_PTON`; `NO_ASN_TIME` force-disables `HAVE_SESSION_TICKET`
  so `quic_test.c` supplies a no-op `wolfSSL_CTX_UseSessionTicket`.
- **`sin_addr` macro collision**: FreeRTOS-Plus-TCP `#define`s `sin_addr`; `quic_test.c`
  `#undef`s it before the ngtcp2 headers, uses `freertos_sockaddr.sin_address.ulIP_IPv4`.
- **newlib syscalls** (`quic_syscalls.c`): ngtcp2 `abort()` + TLS stdio →
  LOGUART / a small static heap.

## Open / TODO

- **flaky DHCP — ROOT-CAUSED & FIXED (2026-06-09): AP PMF `required`.** The AP
  (MikroTik) had `wifi1 security.management-protection=required`; the chip is the only
  wpa2-psk station (others are wpa3) and its RTL8721D 802.11w/PMF support is unstable —
  under `required` the association "succeeds" but the encrypted data path is half-up and
  the DISCOVER never goes out (0.0.0.0). Setting `wifi1 security.management-protection=
  allowed` made DHCP stable: two clean boots both got `192.168.4.244` + QUIC 4.5/2.4
  (vs 3-4 straight `0.0.0.0` under `required`). `allowed` leaves the wpa3 clients on PMF
  (no security loss); per-MAC override isn't possible (this RouterOS wifi `access-list`
  has no `security` field). Revert: `/interface wifi set wifi1 security.management-protection=required`.
  **Tried chip-side PMF** (`wifi_set_mfp_support`→`rltk_set_pmf`, CONFIG_IEEE80211W is on) to let
  the AP keep `required`: the call works (auth + 4-way + key install all succeed) but the RTL8721D
  PMF *data path* is unreliable — MFP=1 got an IP ~1/3 of boots, MFP=2 0/2, vs MFP=0 + AP `allowed`
  2/2. So the chip stays **MFP=0** (`wifi_set_mfp_support(0)` in main.c) and the AP runs `allowed`.
  Capture how-to below stays valid for future link debugging. *(original symptom:*
  **Diagnosed (router-side sniffer, 2026-06-09):** on a failing boot the chip emits
  **zero DHCP packets on *any* AP interface** (incl. its own wifi1) — the only DHCP
  traffic captured is the host's own renew on ether5. The MikroTik registration-table
  shows the chip on wifi1/wpa2-psk with a good signal but `LAST-ACTIVITY` stuck and
  `UPTIME` not refreshing across chip resets (AP keeps the stale session). Other
  clients are wpa3-psk; the chip is the only wpa2-psk station and this AP runs PMF.
  → Root cause: after a chip reset the association/auth re-registers on the AP but the
  **encrypted data path isn't really up** — the chip's DISCOVER is never sent / is
  dropped by the AP on a 4-way-handshake/PMF key-state mismatch (dropped pre-decrypt,
  so even the wifi interface sniffer sees nothing). Not a QUIC/TF-M issue — it's the
  RTL8721D closed `lib_wlan.a` re-association on a PMF AP. Fix dirs: chip-side deauth
  + confirm key install before starting the IP stack; or AP-side give the chip MAC a
  wpa2-only/no-PMF profile or shorten station inactivity timeout. `host tshark` on
  External-Bridge sees 0 packets (DHCP doesn't flood to the wired port) — must sniff
  on the router (`/tool sniffer` buffered + `packet print`; `sniffer quick` emits
  terminal control sequences over ssh and is unusable in a pipe).
- **ChaCha20-Poly1305 speed-up** — blocked on a ChaCha-capable native Windows server
  (Schannel is AES-GCM-only); would need mingw-built wolfSSL/OpenSSL `quic_perf`.

> History: the full SecureFault/AUVIOL boot-hang JTAG investigation (pre-TF-M) is in
> this file's git history — kept out of the live doc now that `TFM=ON` boots cleanly.
