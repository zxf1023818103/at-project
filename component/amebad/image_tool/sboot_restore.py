#!/usr/bin/env python3
"""
Rebuild build/sboot/sboot_priv.pem (+ sboot_pk.h) from a known Ed25519 private-key SEED.

build/sboot/ is gitignored and regenerated with a NEW random key whenever it is missing (fresh
clone, distclean, a different machine/CI). On a secure-boot-FUSED board only an image signed with
the key matching the eFuse 0x1A0 public key boots - a new key produces an image the board rejects.
This restores the ORIGINAL signing key from its 32-byte seed so `make ... SBOOT=ON` signs correctly
again. The seed is recorded per project in doc/test_device_keys.csv (column
sboot_privkey_ed25519_seed); for production, archive that seed in your key store / HSM.

  python3 sboot_restore.py                 # seed from doc/test_device_keys.csv (first data row)
  python3 sboot_restore.py --seed <64hex>  # restore from an explicit seed
"""
import argparse
import csv
import os
import sys

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives import serialization

import sboot_keygen   # same dir: reuse SBOOT_DIR / PRIV_PEM / raw_pubkey / emit_header

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
CSV_PATH = os.path.join(REPO, "doc", "test_device_keys.csv")
SEED_COL = "sboot_privkey_ed25519_seed"


def seed_from_csv():
    if not os.path.exists(CSV_PATH):
        sys.exit("no %s and no --seed given" % CSV_PATH)
    with open(CSV_PATH) as f:
        for row in csv.DictReader(f):
            v = (row.get(SEED_COL) or "").strip()
            if v:
                return v
    sys.exit("column %s not found / empty in %s" % (SEED_COL, CSV_PATH))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", help="32-byte Ed25519 seed as 64 hex chars (default: read from the CSV)")
    args = ap.parse_args()

    seed_hex = (args.seed or seed_from_csv()).strip()
    seed = bytes.fromhex(seed_hex)
    if len(seed) != 32:
        sys.exit("seed must be 32 bytes (64 hex chars), got %d" % len(seed))

    key = Ed25519PrivateKey.from_private_bytes(seed)
    os.makedirs(sboot_keygen.SBOOT_DIR, exist_ok=True)
    pem = key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.PKCS8,
        encryption_algorithm=serialization.NoEncryption(),
    )
    with open(sboot_keygen.PRIV_PEM, "wb") as f:
        f.write(pem)
    pk = sboot_keygen.raw_pubkey(key)
    sboot_keygen.emit_header(pk)
    print("[sboot] restored %s + sboot_pk.h from seed" % sboot_keygen.PRIV_PEM)
    print("[sboot] public key = %s  (must match the eFuse 0x1A0 PK)" % pk.hex())


if __name__ == "__main__":
    sys.exit(main())
