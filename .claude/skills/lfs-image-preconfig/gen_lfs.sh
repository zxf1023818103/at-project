#!/usr/bin/env bash
# Generate a PRE-CONFIGURED LittleFS image (lfs.bin) for AT-project devices — env vars, AWS
# cert/key, a (self-signed) CA, and arbitrary files, baked in so a freshly-flashed board boots
# already provisioned. Geometry is FIXED to match the runtime mount on every platform (bl602 /
# bl616 / bl616cl / AmebaD / Dplus / Green2): region 0x300000, 960 KB, block 4096, page 256.
# Flash the result at raw 0x300000 (or dd it into a whole_flash image at seek=768*4096).
#
# Storage conventions (device side):
#   env var        -> file  KV/<NAME>          (content = value; loaded into environ by env_init)
#   AWS client cert-> file  aws_cert.der (DER) + env cert=aws_cert.der
#   AWS client key -> file  aws_key.der  (DER) + env key=aws_key.der   (LittleFS key backend only;
#                                                 NOT the TF-M ITS secret store — see NOTE below)
#   server CA      -> file  aws_ca.der   (DER) + env ca=aws_ca.der     (self-signed or a root CA)
#   custom file    -> placed verbatim at the given destination path
set -euo pipefail

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SELF_DIR/../../.." && pwd)"

# ---- fixed LFS geometry (doc/lfs.md — changing these makes the image unmountable) ----
BLOCK=4096; PAGE=256; SIZE=983040    # 960 KB = 240 x 4 KB; top 64 KB (0x3F0000+) reserved for TF-M

OUT="lfs_preconfig.bin"
declare -a ENV_PAIRS=()      # NAME=VALUE
declare -a FILES=()          # src[:dest]
AWS_CERT=""; AWS_KEY=""; AWS_CA=""

usage() {
    sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    cat <<'EOF'

Usage: gen_lfs.sh [options]
  --out <path>                 output image (default: lfs_preconfig.bin)
  --wifi-ssid <ssid>           -> KV/WIFI_SSID
  --wifi-password <pw>         -> KV/WIFI_PASSWORD
  --ntp-server <host>          -> KV/NTP_SERVER
  --dns-server <ip>            -> KV/DNS_SERVER
  --tz <tz>                    -> KV/TZ            (e.g. CST-8)
  --endpoint <host>            -> KV/endpoint      (AWS IoT ATS endpoint)
  --env NAME=VALUE             -> KV/NAME          (any extra env var; repeatable)
  --aws-cert <cert.pem|der>    -> aws_cert.der + KV/cert
  --aws-key  <key.pem|der>     -> aws_key.der  + KV/key   (file-backed key; see NOTE)
  --ca       <ca.pem|der>      -> aws_ca.der   + KV/ca    (self-signed CA or root CA)
  --file <src>[:<dest>]        -> copy src into the image at <dest> (default: basename); repeatable
  -h|--help

NOTE: the file-backed AWS key (aws_key.der) is for images WITHOUT the TF-M secret store. On an
AmebaD TF-M image the private key lives in TF-M ITS and CANNOT be pre-seeded via lfs.bin — provision
it over the console after flashing (aws key <b64>). cert/CA/endpoint/env all pre-seed fine either way.
EOF
}

# PEM (any single block) -> DER == base64-decode(body), exactly what the device stores. A .der input
# is copied through unchanged.
to_der() {
    local in="$1" out="$2"
    if head -c 32 "$in" | grep -q -- '-----BEGIN'; then
        awk '/-----BEGIN/{f=1;next} /-----END/{f=0} f' "$in" | tr -d '[:space:]' | base64 -d > "$out"
    else
        cp "$in" "$out"    # already DER
    fi
    [ -s "$out" ] || { echo "gen_lfs: empty DER from $in" >&2; exit 1; }
}

while [ $# -gt 0 ]; do
    case "$1" in
        --out)           OUT="$2"; shift 2;;
        --wifi-ssid)     ENV_PAIRS+=("WIFI_SSID=$2"); shift 2;;
        --wifi-password) ENV_PAIRS+=("WIFI_PASSWORD=$2"); shift 2;;
        --ntp-server)    ENV_PAIRS+=("NTP_SERVER=$2"); shift 2;;
        --dns-server)    ENV_PAIRS+=("DNS_SERVER=$2"); shift 2;;
        --tz)            ENV_PAIRS+=("TZ=$2"); shift 2;;
        --endpoint)      ENV_PAIRS+=("endpoint=$2"); shift 2;;
        --env)           ENV_PAIRS+=("$2"); shift 2;;
        --aws-cert)      AWS_CERT="$2"; shift 2;;
        --aws-key)       AWS_KEY="$2"; shift 2;;
        --ca)            AWS_CA="$2"; shift 2;;
        --file)          FILES+=("$2"); shift 2;;
        -h|--help)       usage; exit 0;;
        *) echo "gen_lfs: unknown arg '$1'" >&2; usage; exit 1;;
    esac
done

# ---- locate (or build) the mklittlefs host tool ----
MKLFS="$REPO/project/.lfs_build/_mklittlefs_install/bin/mklittlefs"
if [ ! -x "$MKLFS" ]; then
    echo "[gen_lfs] building mklittlefs ..." >&2
    cmake -S "$REPO/project" -B "$REPO/project/.lfs_build" >/dev/null
    cmake --build "$REPO/project/.lfs_build" --target lfs_image >/dev/null 2>&1 || true
    [ -x "$MKLFS" ] || MKLFS="$(find "$REPO/project/.lfs_build" -name mklittlefs -type f -executable | head -1)"
    [ -x "$MKLFS" ] || { echo "gen_lfs: mklittlefs not found/built" >&2; exit 1; }
fi

# ---- stage the LFS root ----
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/KV"

for pair in "${ENV_PAIRS[@]}"; do
    name="${pair%%=*}"; val="${pair#*=}"
    [ -n "$name" ] || { echo "gen_lfs: empty env name" >&2; exit 1; }
    printf '%s' "$val" > "$STAGE/KV/$name"
    echo "  KV/$name = $([ "$name" = WIFI_PASSWORD ] && echo '********' || echo "$val")"
done

if [ -n "$AWS_CERT" ]; then to_der "$AWS_CERT" "$STAGE/aws_cert.der"; printf 'aws_cert.der' > "$STAGE/KV/cert"; echo "  aws_cert.der ($(stat -c%s "$STAGE/aws_cert.der") B) + KV/cert"; fi
if [ -n "$AWS_KEY"  ]; then to_der "$AWS_KEY"  "$STAGE/aws_key.der";  printf 'aws_key.der'  > "$STAGE/KV/key";  echo "  aws_key.der  ($(stat -c%s "$STAGE/aws_key.der") B) + KV/key"; fi
if [ -n "$AWS_CA"   ]; then to_der "$AWS_CA"   "$STAGE/aws_ca.der";   printf 'aws_ca.der'   > "$STAGE/KV/ca";   echo "  aws_ca.der   ($(stat -c%s "$STAGE/aws_ca.der") B) + KV/ca"; fi

for spec in "${FILES[@]}"; do
    src="${spec%%:*}"; dest="${spec#*:}"; [ "$dest" = "$spec" ] && dest="$(basename "$src")"
    [ -f "$src" ] || { echo "gen_lfs: --file src not found: $src" >&2; exit 1; }
    mkdir -p "$STAGE/$(dirname "$dest")"; cp "$src" "$STAGE/$dest"; echo "  file $dest ($(stat -c%s "$src") B)"
done

# ---- pack ----
"$MKLFS" -c "$STAGE" -b "$BLOCK" -p "$PAGE" -s "$SIZE" "$OUT" >/dev/null
sz=$(stat -c%s "$OUT")
[ "$sz" -eq "$SIZE" ] || { echo "gen_lfs: output size $sz != $SIZE" >&2; exit 1; }
echo "[gen_lfs] wrote $OUT ($sz B, block=$BLOCK page=$PAGE) — flash at 0x300000 / dd seek=768 bs=4096"
