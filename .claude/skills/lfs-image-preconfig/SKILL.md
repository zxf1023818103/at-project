---
name: lfs-image-preconfig
description: Generate a PRE-CONFIGURED LittleFS image (lfs.bin) for AT-project devices — bake in env vars (WIFI_SSID/WIFI_PASSWORD/NTP_SERVER/DNS_SERVER/TZ + any custom), AWS client cert/key, a (self-signed) CA, and arbitrary files, so a freshly-flashed board boots already provisioned. Fixed cross-platform geometry (0x300000, 960 KB, 4096/240/256). The bl602 + amebad flash skills call this to inject config, passing the params straight through.
argument-hint: --wifi-ssid S --wifi-password P [--ntp-server H] [--dns-server IP] [--tz CST-8] [--endpoint H] [--env K=V ...] [--aws-cert cert.pem] [--aws-key key.pem] [--ca ca.pem] [--file src[:dest] ...] [--out lfs.bin]
allowed-tools: [Bash, Read, Edit]
---

# Pre-configured LittleFS image (lfs.bin) generator

Packs a device's non-volatile config into a LittleFS image so a fresh flash comes up already
provisioned — no console `setenv` / `aws …` dance afterward. Script:
`.claude/skills/lfs-image-preconfig/gen_lfs.sh` (runs in WSL; builds the `mklittlefs` host tool
from the repo's `component/lfs_image` on first use).

## What goes in, and where the device reads it

| Input | In the image | Device read path |
|---|---|---|
| `--wifi-ssid` / `--wifi-password` | `KV/WIFI_SSID` / `KV/WIFI_PASSWORD` | env → `wifi_service` connects at boot |
| `--ntp-server` / `--dns-server` / `--tz` | `KV/NTP_SERVER` / `KV/DNS_SERVER` / `KV/TZ` | env (SNTP / resolver / newlib tz) |
| `--endpoint` | `KV/endpoint` | env — AWS IoT ATS endpoint |
| `--env NAME=VALUE` (repeatable) | `KV/NAME` | env (`env_init` loads every `KV/*` into `environ`) |
| `--aws-cert cert.pem` | `aws_cert.der` + `KV/cert` | client cert DER; env `cert` = path |
| `--aws-key key.pem` | `aws_key.der` + `KV/key` | client key DER (file backend — see NOTE) |
| `--ca ca.pem` | `aws_ca.der` + `KV/ca` | server-trust CA DER (self-signed or root CA); env `ca` = path |
| `--file src[:dest]` (repeatable) | `dest` (verbatim) | app-specific |

`env` file content **is the value** (no header/CRC). PEM inputs are converted to DER exactly as
the device's `aws cert/ca/key <b64>` path stores them (strip armor → base64-decode); a `.der`
input is passed through. `KV/<name>` naming: no `/` or `}` (see doc/kv.md).

**NOTE — TF-M key:** the file-backed `aws_key.der` is for images WITHOUT the TF-M secret store
(e.g. bl602, or amebad `TFM=OFF`). On an **amebad TF-M image the private key lives in TF-M ITS and
CANNOT be pre-seeded via lfs.bin** — flash the pre-config for everything else, then provision only
the key over the console (`aws key <b64>`, or `flash.ps1 -Key`). cert/CA/endpoint/env all pre-seed.

## Geometry (fixed — do not change)

`0x300000` start, **960 KB** (`983040` B = 240 × 4 KB; top 64 KB `0x3F0000+` reserved for TF-M),
`block=4096 page=256`. One image mounts on every platform (bl602/bl616/bl616cl/AmebaD/Dplus/Green2)
— `block_count` is in the LFS superblock, so the geometry MUST match `component/littlefs_config`
(see doc/lfs.md). The script hardcodes `mklittlefs -b 4096 -p 256 -s 983040`.

## Generate

```bash
.claude/skills/lfs-image-preconfig/gen_lfs.sh \
  --out /tmp/lfs_pre.bin \
  --wifi-ssid ZengOTA --wifi-password 123456789 --tz CST-8 \
  --ntp-server pool.ntp.org --dns-server 8.8.8.8 \
  --endpoint a1xxxx-ats.iot.ap-southeast-1.amazonaws.com \
  --aws-cert build/aws/cert.pem --ca build/aws/AmazonRootCA1.pem \
  --env LOG_LEVEL=info --file myconf.json:conf/myconf.json
```
Output = a 983040-byte `lfs.bin`.

## Inject it at flash time (how the flash skills call this)

The image lands at raw flash `0x300000`. Two ways, both used by the flash skills:

1. **Embed into the whole-flash image (recommended for a fresh board)** — `dd` it into the 4 MB
   whole-flash at `seek=768` (768 × 4 KB = 0x300000), then flash the whole image once:
   ```bash
   dd if=/tmp/lfs_pre.bin of=<whole_flash_data.bin> bs=4096 seek=768 conv=notrunc status=none
   ```
2. **Flash the region on its own** at `0x300000` (leaves firmware alone) — the bl602 UART tool
   supports `--single --firmware lfs.bin --addr 0x300000`.

`[[bouffalo-cklink-flash]]` (bl602) and `[[amebad-imagetool-flash]]` both take the same config
flags and forward them here, then embed the result — see their "config injection" sections.

## Verified (2026-07-11, HW)

**Env pre-seed (bl602):** `gen_lfs.sh --wifi-ssid ZengOTA --wifi-password … --tz CST-8 --env
PRESEED_MARK=…` → embed into whole_flash → `-Mode whole` → board booted straight to `[WSVC] ZengOTA
found -> connect` → `network UP`, and `env` listed every baked-in var (the whole-flash wiped any
prior LFS first).

**AWS pre-seed (endpoint + EC client cert/key + Amazon Root CA, all in lfs.bin):**
- **bl602** `make AWS=ON` (lean telemetry client, picotls): booted → WiFi → `[AWS] TLS connect …:8883`
  → picotls handshake PASS (server cert verified vs the pre-seeded CA; client cert `a81710cf6427`) →
  `[AWS] connected to AWS IoT Core` → **`[AWS] published seq=0 to bl602/a81710cf6427/telemetry`**. ✅
- **amebad `make AWS=ON TFM=OFF`** (cert+key in LFS, aligned with bl602 — TF-M ITS is bypassed):
  `[AWS] provisioned: endpoint=… certPath=aws_cert.der key=121B` → TLS handshake PASS → **`[AWS] MQTT
  connected to AWS IoT Core as '2442e3458d85'`** using the LFS-stored EC cert/key. The AWS connection
  (telemetry's transport) is up; the `aws_iot_demo` telemetry publish (`[AWS] TX bw20/…`) is gated
  behind `ota_app_is_active()` and stays paused while the OTA agent churns a job doc — an amebad
  OTA-agent / stale-job matter, NOT a pre-config issue (bl602's lean client has no such gate). The
  file-backed key path is what `TFM=OFF` selects (`aws_secret_store_file.c`).
