#!/usr/bin/env bash
# Build the native (WSL2/x86-64) quic_perf throughput peer against the in-tree
# ngtcp2 + wolfSSL libraries built under build/host/.  Run host-side build of
# those first (see doc/amebad_quic_test.md) — this script assumes they exist.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"

WROOT="$ROOT/build/host/wolfssl/_install"
NG="$ROOT/build/host/ngtcp2"
NGSRC="$ROOT/component/ngtcp2_config/ngtcp2"

# Throwaway self-signed ECDSA P-256 cert for the perf server (the chip runs
# SSL_VERIFY_NONE, so any self-signed cert works — not a real secret). Generated
# on demand and gitignored, so no private key ever lands in the repo.
if [ ! -f "$HERE/cert.pem" ] || [ ! -f "$HERE/key.pem" ]; then
  openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes \
    -keyout "$HERE/key.pem" -out "$HERE/cert.pem" -days 3650 \
    -subj "/CN=quic-perf-test" >/dev/null 2>&1
  echo "generated throwaway cert.pem/key.pem"
fi

gcc -O2 -Wall -Wextra -o "$HERE/quic_perf" "$HERE/quic_perf.c" \
  -I"$WROOT/include" \
  -I"$NGSRC/lib/includes" -I"$NG/lib/includes" \
  -I"$NGSRC/crypto/includes" \
  "$NG/crypto/wolfssl/libngtcp2_crypto_wolfssl.a" \
  "$NG/lib/libngtcp2.a" \
  "$WROOT/lib/libwolfssl.a" \
  -lm -lpthread

echo "built: $HERE/quic_perf"
