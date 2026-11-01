# Wi-Fi-only validation — 2026-11-01

This image runs Wi-Fi with FreeRTOS+TCP on BL618DG B0, using official SDK
`237ed937856bf435f4f3642ea4728294723e1d87`. Bluetooth Controller, Bluedroid and
lwIP are not linked or initialized. GPIO16 is held low to select Wi-Fi.
The normal Wi-Fi/combo RF calibration is used; standalone Bluetooth PLL
calibration is not part of this test.

## Build and flash

From the repository root:

```sh
make -C project/bl618dg_pan whole WIFI_ONLY=ON OUT=/tmp/bl618dg-wifi-only-build
```

The tested image is also copied to
`project/bl618dg_pan/build/wifi_only/whole_flash_data.bin`. In Windows PowerShell
from the validation project directory:

```powershell
.\tools\flash.ps1 -Fw .\build\wifi_only\whole_flash_data.bin
```

COM15 is the data/programming port at 2,000,000 baud; COM23 controls BOOT/reset.
The flash script verifies the image and preserves PSM pairing storage.
The default build without `WIFI_ONLY=ON` remains the separate PAN application.

## Connect and verify

```text
wifi_scan
wifi_connect "Zeng" "<password>" 2467
wifi_status
wifi_http
wifi_disconnect
wifi_connect "Zeng" "<password>" 5240
wifi_status
wifi_http
```

The last argument is the AP's primary frequency in MHz. Use the current
frequency of your AP. DHCP supplies the local address, gateway and DNS.
`wifi_http` binds its socket to the STA address and requests example.com over
plain HTTP. There is no automatic credential storage or reconnect after reset.

For recorded runs, use `tools/wifi_connect.ps1 -CredentialFile <private.json>
-Frequency 2467`; its private JSON has `ssid` and `password` fields. The helper
removes the password from captured UART echoes before output. Test credentials
are fetched through the user's RouterOS SSH access and are not committed.
The router's configuration is not modified.

## Network adapter correction

FHOST creates two logical VIF handles, `wl1` (STA) and `wl2` (AP). The adapter
now registers only STA with FreeRTOS+TCP. A dormant AP endpoint with address and
mask `0.0.0.0/0` previously matched off-subnet destinations before the STA
gateway, preventing TCP connections. The unconfigured STA endpoint also uses a
host mask until DHCP supplies its actual mask. Unsupported AP IP services
remain disabled.

## Hardware results

Both runs use the same final image, MAC `B4:E8:42:3C:B5:2A`, SSID `Zeng`, and
DHCP address `192.168.100.7` (gateway/DNS `192.168.100.1`). RouterOS independently
confirmed `wifi1` / 2ghz-n and `wifi2` / 5ghz-ac respectively.

| Test | 2.4 GHz, requested 2467 MHz | 5 GHz, 5240 MHz |
|---|---|---|
| RF / FHOST / MGMR startup | PASS | Same initialized radio |
| WPA2 association and DHCP | PASS | PASS after switching bands |
| DNS + TCP + HTTP | `HTTP/1.1 200 OK` | `HTTP/1.1 200 OK` |
| 1472-byte ICMP payload, DF (1500-byte IPv4) | 3/3, 4–12 ms, mean 7 ms | 3/3, 4–7 ms, mean 5 ms |

Wi-Fi scan also completed and found both test APs. The flash tool verified the
image. SHA-256 of the delivered whole image:
`c734fd382c42d64ed08f05b3a6be0070ca10a9afaa2755e3ae13acda55371611`.
The board is left connected on 5 GHz. Final adapter counters: RX 195, TX 43,
RX/TX drops both 0. Raw, password-redacted test logs are kept
locally under the ignored `build/validation/wifi_only` directory.

The other PAN/coexistence build was also compiled after the shared network-code
changes; Bluetooth was never started during this hardware test. These are
short functional tests, without throughput or long-duration qualification.
Concurrent Wi-Fi/PAN operation remains a separate, unpassed test; see
[coexistence validation](COEX_VALIDATION.md).
