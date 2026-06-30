---
name: amebad-quic-speedtest
description: Run the Windows-native C# QUIC "perf" server to LAN speed-test the AmebaD (RTL8721D) chip's ngtcp2 QUIC client (download/upload Mbps), e.g. to compare software vs HW AES-GCM. Covers why the server MUST be native Windows (WSL2 drops inbound UDP), starting it detached from WSL, the chip side, reading results, and the port/cert/dotnet/firewall gotchas.
argument-hint: [--port 4567] [--bind 0.0.0.0]
allowed-tools: [Bash, Read, Edit]
---

# AmebaD QUIC LAN speed-test (Windows-native server)

Drives the chip's ngtcp2 **"perf"** client (`project/amebad/app_km4/quic_test.c`) against a
native-Windows **System.Net.Quic** server and reads throughput from both ends. Used to measure QUIC
throughput and to A/B software vs HW AES-GCM (`HWGCM=ON/OFF`).

> **Why the server runs on Windows, not WSL.** WSL2 *mirrored* networking does NOT deliver inbound
> **UDP** to a WSL socket (inbound TCP works — iperf to WSL is fine — but QUIC is UDP, so a WSL
> listener receives 0 packets). Confirmed repeatedly. The server therefore runs as a **native
> Windows process**; only that receives the chip's QUIC Initial. See the
> `project_amebad_quic_speedtest` memory for the full forensic trail.

## The "perf" protocol (so client/server stay in sync)

Client opens one bidi stream → sends an 8-byte big-endian uint64 = **download** byte count → server
returns exactly that many bytes + FIN → client then streams the **upload** bytes + FIN; server counts
them. ALPN is `"perf"`. The chip runs `SSL_VERIFY_NONE`, so a self-signed server cert is fine.

## Components / paths (verify each session — they drift)

- **Server source (canonical, in repo):** `project/amebad/quic_perf/win/{Program.cs,quicperf.csproj}`
  — System.Net.Quic/msquic, ALPN "perf", self-signed **ECDSA P-256**, prints per-transfer
  `DOWNLOAD (chip RX): <n> B in <ms> = <Mbps> [ok]` / `UPLOAD (chip TX): ...` lines.
- **dotnet:** `C:\Users\zxf10\.dotnet\dotnet.exe` — per-user **.NET 8.0.421**, NOT on PATH, installed
  without admin (`dotnet-install.ps1 -Channel 8.0 -InstallDir $env:USERPROFILE\.dotnet -NoPath`).
- **Run dir (Windows-local):** `C:\Users\zxf10\quicperf_win` — dotnet dislikes a `\\wsl.localhost`
  UNC cwd, so the project is copied here to build/run. `srv.log`/`srv.err` land here.
- **Chip client target:** `project/amebad/app_km4/quic_test.c` — `QUIC_SERVER_A0..A3` = host IP
  (currently **192.168.4.250**), `QUIC_SERVER_PORT` = **4567**, `QUIC_ALPN` = "perf",
  `QUIC_PERF_XFER_BYTES` = 4 MB/direction. Edit + rebuild if the host IP/port changes.

## Procedure

### 1. Confirm the host is on the chip's LAN
The chip is a STA on AP `Zeng`; the server host must be reachable at `QUIC_SERVER_*` (192.168.4.250).
```bash
powershell.exe -NoProfile -Command "Get-NetIPAddress -AddressFamily IPv4 | ? { \$_.IPAddress -notlike '127.*' } | ft IPAddress,InterfaceAlias -Auto"
```
Expect `192.168.4.250` on `vEthernet (External-Bridge)`. If the host IP differs, either set it static
or edit `QUIC_SERVER_A0..A3` in `quic_test.c` and rebuild.

### 2. Sync the server to the Windows-local run dir
```bash
powershell.exe -NoProfile -Command "robocopy '\\wsl.localhost\Ubuntu\home\zxf10\at-project\project\amebad\quic_perf\win' 'C:\Users\zxf10\quicperf_win' /MIR /XD bin obj /NFL /NDL /NJH /NJS; exit 0"
```
(`robocopy` exit codes are non-zero on success — the `exit 0` swallows it.)

### 3. Start the server detached (Windows-native), logging to srv.log
```bash
powershell.exe -NoProfile -Command "Set-Content 'C:\Users\zxf10\quicperf_win\srv.log' ''; Set-Content 'C:\Users\zxf10\quicperf_win\srv.err' ''; \$p = Start-Process -FilePath 'C:\Users\zxf10\.dotnet\dotnet.exe' -ArgumentList 'run','-c','Release','--project','C:\Users\zxf10\quicperf_win','--','0.0.0.0','4567' -RedirectStandardOutput 'C:\Users\zxf10\quicperf_win\srv.log' -RedirectStandardError 'C:\Users\zxf10\quicperf_win\srv.err' -WindowStyle Hidden -PassThru; Write-Output \"SRV_PID=\$(\$p.Id)\""
```
- Bind `0.0.0.0` (the listener actually binds `::` dual-stack and still receives the chip's **IPv4**
  — verified). An explicit `192.168.4.250` also works.
- `dotnet run` first does an incremental build (10–30 s) before listening. Confirm it's up:
  ```bash
  powershell.exe -NoProfile -Command "Get-NetUDPEndpoint -LocalPort 4567 | ft LocalAddress,LocalPort,OwningProcess -Auto"
  ```
  The owner is the **apphost** child (`quicperf_win`), not the `dotnet run` wrapper — so it won't
  appear under `Get-Process dotnet`. That's expected.

### 4. Build + flash a QUIC image, then let the chip loop
```bash
cd project/amebad && make QUIC=ON TFM=ON          # HW-GCM is the default (HWGCM=ON); HWGCM=OFF for software A/B
```
Flash with the `amebad-imagetool-flash` skill / `flash.ps1 -BootPort COM14 -FlashPort COM3 -Image <UNC firmware.bin> -Baudrate 1500000`.
The chip loops perf rounds forever after boot — no host trigger needed.

### 5. Read results
- **Server side (authoritative):**
  ```bash
  powershell.exe -NoProfile -Command "Get-Content 'C:\Users\zxf10\quicperf_win\srv.log' -Tail 8"
  ```
  → `DOWNLOAD (chip RX): 4194304/4194304 B in <ms> = <Mbps> [ok]` (chip **decrypt** path) and
  `UPLOAD (chip TX): ... = <Mbps> [ok]` (chip **encrypt** path).
- **Chip side:** `console.ps1 -Port COM3 -ResetPort COM14 -Seconds 80` prints
  `LAN speed test (round N) / DOWNLOAD (RX): <kbps> / UPLOAD (TX): <kbps>` and a per-second
  `.hb so/rx/ack/sr/fin` heartbeat. A full round is ~7 s DL + ~14 s UL on a clean 5 GHz link, so
  capture ≥80 s to catch boot + one round.

### 6. Stop the server when done
```bash
cmd.exe /c "taskkill /F /IM quicperf_win.exe /T" 2>/dev/null; cmd.exe /c "taskkill /F /IM dotnet.exe /T" 2>/dev/null
```

## Reference numbers (sanity baseline, clean 5 GHz `Zeng` ch64, picotls/mbedTLS)

| AES-GCM | Download (chip decrypt) | Upload (chip encrypt) |
|---|---|---|
| **HW** (`HWGCM=ON`, default) | ~7.1 Mbps | ~2.9 Mbps |
| software (`HWGCM=OFF`)        | ~1.7 Mbps | ~1.2 Mbps |

Throughput is **WiFi-link-sensitive**: on a congested 2.4 GHz association (or a weak/${far link) the
same firmware can drop to ~1–2 Mbps with incomplete transfers. Always confirm the chip associated on
**5 GHz (ch36–165, RSSI better than ~-50)** before trusting a number; the scan handler already prefers
5 GHz and retries.

## Gotchas (each cost real time once)

- **Inbound UDP to WSL is dead** (mirrored mode) → server MUST be native Windows. WSL `iperf` (TCP)
  working does not imply QUIC (UDP) will.
- **Port 4567, not 4433.** WSL having once bound 4433 permanently blocks a Windows bind there
  (10048 address-in-use, no visible owner). 4567 is clean.
- **dotnet not on PATH**; use the full `C:\Users\zxf10\.dotnet\dotnet.exe`. The `.exe` apphost run
  directly says "must install .NET" → always `dotnet run` / `dotnet <dll>`.
- **net8 QUIC is preview**: the csproj sets `<EnablePreviewFeatures>true` + `NoWarn SYSLIB5001;CA2252`.
  Needs Win11 22H2+ (Schannel TLS 1.3).
- **Self-signed cert key flags:** import with `Exportable|PersistKeySet|UserKeySet` — `EphemeralKeySet`
  makes Schannel return `QUIC_STATUS_CERT_NO_CERT` and the handshake fails.
- **Firewall:** the native-Windows process is allowed; a WSL-hosted listener is blocked and
  `New-NetFirewallRule` needs admin.
- **Lingering Windows procs:** a timed-out `powershell.exe`/`dotnet.exe` started from WSL keeps running
  on the Windows side and holds COM3 / the UDP port → later reads get 0 bytes / bind fails. Clean with
  `cmd.exe /c "taskkill /F /IM dotnet.exe"` (and `/IM powershell.exe`). Start the server with
  `Start-Process ... -PassThru` (detached) — do NOT run it as a foreground WSL background job that the
  harness may SIGSTKFLT.
- **`pkill -f` self-kill:** `pkill -f quicperf`/`openocd` matches your own command line and kills your
  shell — use Windows `taskkill /IM` instead.
