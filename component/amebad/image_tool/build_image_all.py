#!/usr/bin/env python3
"""
Build the driver-only AmebaD (RTL8721D) flashable Image_All.bin.

The KM0 and KM4 halves are separate CMake builds (different toolchain files), so
this orchestration lives above CMake. It:

  1. configures + builds the KM0 tree   -> km0_boot_all.bin, km0_image2_all.bin
  2. configures + builds the KM4 tree   -> km4_boot_all.bin, km4_image2_all.bin
     (target_img2_km4 is EXCLUDE_FROM_ALL, so build the targets explicitly)
  3. cross-core-merges the image2 halves: km0_km4_image2 = km0_image2 + km4_image2
     (imagetool.sh's RSIP-off branch is a plain cat)
  4. lays the three pieces into one 0xFF-padded flash image at their offsets:
        0x00000  km0_boot_all.bin        (flash 0x08000000)
        0x04000  km4_boot_all.bin        (flash 0x08004000)
        0x06000  km0_km4_image2.bin      (flash 0x08006000 = OTA1, OTA_Region[0])

RSIP: off by default (plain image, for boards without RSIP fused). For an
RSIP-fused board, rebuild both trees with -DAMEBAD_RSIP_ENABLE=ON and the *_all
bins come out encrypted; this script then just cats/lays them the same way.

Usage (preferred — via the CMake wrapper, from either core's build dir):
    cmake --build build/km0 -t amebad_image_all
  or directly:
    python3 component/amebad/image_tool/build_image_all.py [--out PATH] [--no-build]
Output (default): build/amebad/Image_All.bin

The CMake target `amebad_image_all` (defined by amebad_make_image_all_bin() in
component/amebad/image_tool/CMakeLists.txt) is just a thin wrapper that runs this
script — prefer it so the invocation lives in the build, not in muscle memory.
"""
import argparse
import os
import struct
import subprocess
import sys

# This script lives in component/amebad/image_tool/ -> repo root is three up.
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))

# (flash offset, basename, build dir relative path under the build tree)
KM0_BOOT  = ("component/amebad/bootloader_km0/km0_boot_all.bin")
KM0_IMG2  = ("component/amebad/app_km0/km0_image2_all.bin")
KM4_BOOT  = ("component/amebad/bootloader_km4/km4_boot_all.bin")
KM4_IMG2  = ("component/amebad/app_km4/km4_image2_all.bin")
# TF-M SPE blob — read from the BUILD dir (never the source tree): the in-tree
# TF-M build (CMake target tfm_spe) copies tfm_s.bin here.
KM4_TFM_S  = ("component/amebad/app_km4_secure/tfm_s.bin")
# Standalone FreeRTOS TrustZone secure image (make SECURE=ON), built by the CMake
# target target_img3_km4_frtos_s -> objcopy raw binary. Same headerless cached-XIP
# model as tfm_s.bin; laid at the same dynamic IMG3 slot.
KM4_FRTOS_S = ("component/amebad/app_km4_freertos_secure/freertos_s.bin")

OFF_KM0_BOOT  = 0x0000
OFF_KM4_BOOT  = 0x4000
OFF_IMAGE2    = 0x6000     # OTA1 (OTA_Region[0] = LS_IMG2_OTA1_ADDR = 0x08006000)
# tfm_s (the old OFF_IMG3) is now DYNAMIC: laid 4 KB-aligned right after the
# merged image2 (see below), so it never blocks image2 growth. The bootloader
# resolves the same offset at runtime from the image2 segment chain. Upper bound
# = OTA2 start (LS_IMG2_OTA2_ADDR 0x08183000): tfm_s must end before it.
OFF_OTA2      = 0x183000


def run(cmd):
    print("+ " + " ".join(cmd), flush=True)
    subprocess.check_call(cmd, cwd=REPO)


def build(build_dir, targets):
    # Configuration is the Makefile's job — project/amebad/Makefile's `configure`
    # target owns the toolchain file / generator / feature flags and runs before
    # this script (the `image:` target depends on `configure`). Here we only build
    # the requested targets against the already-configured tree; `cmake --build`
    # re-runs CMake itself if a CMakeLists changed. (Configuring here too would
    # re-pass CMAKE_TOOLCHAIN_FILE on the warm tree and warn "Manually-specified
    # variables were not used by the project".)
    run(["cmake", "--build", build_dir, "-t"] + targets)


def read(path):
    if not os.path.exists(path):
        sys.exit("missing build artifact: %s" % path)
    with open(path, "rb") as f:
        return f.read()


def cache_bool(build_dir, key):
    """True if a CMakeCache.txt bool variable is set ON/TRUE/1/YES."""
    cache = os.path.join(build_dir, "CMakeCache.txt")
    if not os.path.exists(cache):
        return False
    with open(cache) as f:
        for line in f:
            if line.startswith(key + ":") and "=" in line:
                return line.split("=", 1)[1].strip().upper() in ("ON", "TRUE", "1", "YES")
    return False


def place(image, offset, blob, name):
    end = offset + len(blob)
    if end > len(image):
        image.extend(b"\xff" * (end - len(image)))
    # nothing already placed should overlap (offsets are partition-aligned)
    image[offset:end] = blob
    print("  %-22s @ 0x%06x  %6d bytes  (ends 0x%06x)" %
          (name, offset, len(blob), end))


# KM4 image2 XIP-maps to this VMA (image_addr field = IMG2_VMA + IMAGE_HEADER_LEN). The boot
# ROM's BOOT_FLASH_Image2SignatureCheck (boot_flash_hp.c) and the on-device `sboot` self-test both
# read the IMAGE_HEADER here.
IMG2_VMA = 0x0E000000


def sign_km4_image2(img2, priv_pem):
    """Ed25519-sign the KM4 image2 exactly the way the boot ROM verifies it.

    The ROM computes len = sb_header - IMG2_VMA - 0x20 and verifies the signature over the bytes
    at [IMG2_VMA+0x20 .. sb_header) (the whole image2 payload, contiguous in the XIP-mapped flash),
    with sb_header pointing at a 4 KB-padded SB_HEADER block appended after the payload (KM0
    boot_flash_lp.c adds 0x1000 to the mapped size when sb_header != 0xFFFFFFFF). So:
      - pad img2 (header+payload) to 4 KB  -> sb_off ; sb_vma = IMG2_VMA + sb_off
      - sign  img2[0x20 : sb_off]
      - SB_HEADER = reserved[48] + sig[64], padded to 0x1000, appended at sb_off
      - patch IMAGE_HEADER.sb_header (header offset 0x10) = sb_vma
    """
    from cryptography.hazmat.primitives import serialization
    with open(priv_pem, "rb") as f:
        key = serialization.load_pem_private_key(f.read(), password=None)

    padded = bytearray(img2)
    if len(padded) % 0x1000:
        padded.extend(b"\xff" * (0x1000 - (len(padded) % 0x1000)))
    sb_off = len(padded)
    sb_vma = IMG2_VMA + sb_off

    sig = key.sign(bytes(padded[0x20:sb_off]))          # Ed25519 over the payload (RFC 8032)
    assert len(sig) == 64

    sb_block = bytearray(b"\x00" * 48 + sig)            # SB_HEADER: reserved[12] (48 B) + sb_sig[64]
    sb_block.extend(b"\xff" * (0x1000 - len(sb_block)))  # pad the signature block to 4 KB

    struct.pack_into("<I", padded, 0x10, sb_vma)         # IMAGE_HEADER.sb_header
    signed = bytes(padded) + bytes(sb_block)
    print("  [sboot] signed KM4 image2: payload 0x%x B, sb_header -> 0x%08x, +0x1000 sig block"
          % (sb_off - 0x20, sb_vma))
    return signed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(REPO, "build", "amebad", "Image_All.bin"))
    ap.add_argument("--km0-build", default=os.path.join(REPO, "build", "km0"))
    ap.add_argument("--km4-build", default=os.path.join(REPO, "build", "km4"))
    ap.add_argument("--no-build", action="store_true",
                    help="skip cmake, assemble from existing build trees")
    args = ap.parse_args()

    tfm_boot = cache_bool(args.km4_build, "AMEBAD_TFM_BOOT")
    frtos_secure = cache_bool(args.km4_build, "AMEBAD_FREERTOS_SECURE")
    sboot = cache_bool(args.km4_build, "AMEBAD_SBOOT")

    if not args.no_build:
        build(args.km0_build, ["target_loader_km0", "target_img2_km0"])
        km4_targets = ["target_loader_km4", "target_img2_km4"]
        if tfm_boot:
            km4_targets.append("tfm_spe")                  # in-tree TF-M SPE -> tfm_s.bin
        if frtos_secure:
            km4_targets.append("target_img3_km4_frtos_s")  # FreeRTOS secure image -> freertos_s.bin
        build(args.km4_build, km4_targets)

    km0_boot = read(os.path.join(args.km0_build, KM0_BOOT))
    km0_img2 = read(os.path.join(args.km0_build, KM0_IMG2))
    km4_boot = read(os.path.join(args.km4_build, KM4_BOOT))
    km4_img2 = read(os.path.join(args.km4_build, KM4_IMG2))

    if sboot:
        priv_pem = os.path.join(REPO, "build", "sboot", "sboot_priv.pem")
        if not os.path.exists(priv_pem):
            sys.exit("AMEBAD_SBOOT: %s not found (run sboot_keygen.py at configure)." % priv_pem)
        km4_img2 = sign_km4_image2(km4_img2, priv_pem)

    # cross-core image2 merge (imagetool.sh RSIP-off branch is a plain cat)
    km0_km4_img2 = km0_img2 + km4_img2

    print("\nassembling Image_All.bin:")
    image = bytearray()
    place(image, OFF_KM0_BOOT, km0_boot,     "km0_boot_all.bin")
    place(image, OFF_KM4_BOOT, km4_boot,     "km4_boot_all.bin")
    place(image, OFF_IMAGE2,   km0_km4_img2, "km0_km4_image2.bin")

    # TF-M boot: tfm_s executes cached-XIP from S_CODE_START 0x0E400000, which a
    # dedicated RSIP MMU entry (programmed by the bootloader) maps to tfm_s's
    # flash slot. That slot is DYNAMIC — 4 KB-aligned right after the merged
    # image2 — so growing image2 never collides with tfm_s. The bootloader
    # computes the identical offset at runtime from the image2 segment chain.
    # Secure image (IMG3): TF-M (tfm_s.bin) or the standalone FreeRTOS secure image
    # (freertos_s.bin). Both are headerless vector-table-first blobs that execute
    # cached-XIP from S_CODE_START 0x0E400000 via a dedicated RSIP MMU entry the
    # bootloader programs; the slot is DYNAMIC — 4 KB-aligned right after the merged
    # image2 — so growing image2 never collides with it. (TFM and SECURE are
    # mutually exclusive at configure time.)
    secure_blob = None
    secure_label = None
    if tfm_boot:
        tfm_path = os.path.join(args.km4_build, KM4_TFM_S)
        if not os.path.exists(tfm_path):
            sys.exit("AMEBAD_TFM_BOOT: %s not found.\n"
                     "  Expected the in-tree TF-M build (CMake target 'tfm_spe') to produce it.\n"
                     "  Run: cmake --build %s -t tfm_spe" % (tfm_path, args.km4_build))
        secure_blob = read(tfm_path)
        secure_label = "tfm_s.bin (TF-M SPE)"
    elif frtos_secure:
        frtos_path = os.path.join(args.km4_build, KM4_FRTOS_S)
        if not os.path.exists(frtos_path):
            sys.exit("AMEBAD_FREERTOS_SECURE: %s not found.\n"
                     "  Expected the CMake target 'target_img3_km4_frtos_s' to produce it.\n"
                     "  Run: cmake --build %s -t target_img3_km4_frtos_s" % (frtos_path, args.km4_build))
        secure_blob = read(frtos_path)
        secure_label = "freertos_s.bin (FreeRTOS TZ secure)"

    if secure_blob is not None:
        off_img3 = (len(image) + 0xFFF) & ~0xFFF        # 4 KB-aligned, right after image2
        if off_img3 + len(secure_blob) > OFF_OTA2:
            sys.exit("secure image (@0x%06x + %d B) overruns OTA2 @0x%06x — image2 too big"
                     % (off_img3, len(secure_blob), OFF_OTA2))
        place(image, off_img3,
              secure_blob,
              "%s, cached XIP @0x0E400000, dynamic @flash 0x%08x"
              % (secure_label, 0x08000000 + off_img3))

    outdir = os.path.dirname(args.out)
    os.makedirs(outdir, exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(image)
    # also emit the merged image2 so the pieces can be flashed individually
    # (preserves the DPK/BACKUP/XIPSYS cal regions that Image_All.bin blanks).
    merged = os.path.join(outdir, "km0_km4_image2.bin")
    with open(merged, "wb") as f:
        f.write(km0_km4_img2)

    # OTA upgrade image, in the stock SDK's Realtek format (gnu_utility/prepend_ota_header.sh +
    # the `checksum` tool): a 32-byte OTA header followed by the OTA slot payload. The payload is
    # everything from OFF_IMAGE2 onward = image2 [+ IMG3 for a TF-M build] — exactly what a
    # device-side OTA download writes into the inactive slot (and what the bootloader maps + resolves
    # IMG3 from), unlike the stock script which is image2-only and so misses the TF-M secure image.
    # Header (8 x 4-byte fields; CHECKSUM/IMAGE_LEN/OFFSET/IMAGE_ADDR little-endian):
    #   VERSION=0xFFFFFFFF  OTA_NUM=1  OTA_SIGN="OTA1"(0x4F544131)  HEADER_LEN=24
    #   CHECKSUM=sum(payload)&0xFFFFFFFF  IMAGE_LEN=len(payload)  OFFSET=32  IMAGE_ADDR=0xFFFFFFFF
    ota_payload = bytes(image[OFF_IMAGE2:])
    ota_header = (
        struct.pack(">I", 0xFFFFFFFF) +                       # VERSION   (as-is)
        struct.pack("<I", 1) +                                # OTA_NUM
        struct.pack(">I", 0x4F544131) +                       # OTA_SIGN  "OTA1" (as-is)
        struct.pack("<I", 24) +                               # HEADER_LEN
        struct.pack("<I", sum(ota_payload) & 0xFFFFFFFF) +    # CHECKSUM
        struct.pack("<I", len(ota_payload)) +                 # IMAGE_LEN
        struct.pack("<I", 32) +                               # OFFSET (header size)
        struct.pack("<I", 0xFFFFFFFF))                        # IMAGE_ADDR
    ota_all = os.path.join(outdir, "OTA_All.bin")
    with open(ota_all, "wb") as f:
        f.write(ota_header + ota_payload)

    print("\n-> %s  (%d bytes)" % (args.out, len(image)))
    print("-> %s  (32 B OTA header + %d B payload = image2%s)"
          % (ota_all, len(ota_payload), " + IMG3" if secure_blob is not None else ""))
    print("\nFlash one of two ways:")
    print("  A. single image (blanks the cal regions):")
    print("       %s  @ 0x08000000" % args.out)
    print("  B. three pieces (keeps existing cal @ 0x08001000-0x08003fff):")
    print("       %-26s @ 0x08000000" % os.path.join(args.km0_build, KM0_BOOT))
    print("       %-26s @ 0x08004000" % os.path.join(args.km4_build, KM4_BOOT))
    print("       %-26s @ 0x08006000" % merged)


if __name__ == "__main__":
    main()
