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

OFF_KM0_BOOT  = 0x0000
OFF_KM4_BOOT  = 0x4000
OFF_IMAGE2    = 0x6000     # OTA1 (OTA_Region[0] = LS_IMG2_OTA1_ADDR = 0x08006000)
OFF_IMG3      = 0x100000   # secure tfm_s @ flash 0x08100000 (cached XIP @0x0E100000)


def run(cmd):
    print("+ " + " ".join(cmd), flush=True)
    subprocess.check_call(cmd, cwd=REPO)


def build(chip, build_dir, targets):
    run(["cmake", "-S", REPO, "-B", build_dir,
         "-DCMAKE_TOOLCHAIN_FILE=cmake/amebad-%s.cmake" % chip])
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(REPO, "build", "amebad", "Image_All.bin"))
    ap.add_argument("--km0-build", default=os.path.join(REPO, "build", "km0"))
    ap.add_argument("--km4-build", default=os.path.join(REPO, "build", "km4"))
    ap.add_argument("--no-build", action="store_true",
                    help="skip cmake, assemble from existing build trees")
    args = ap.parse_args()

    tfm_boot = cache_bool(args.km4_build, "AMEBAD_TFM_BOOT")

    if not args.no_build:
        build("km0", args.km0_build, ["target_loader_km0", "target_img2_km0"])
        km4_targets = ["target_loader_km4", "target_img2_km4"]
        if tfm_boot:
            km4_targets.append("tfm_spe")                  # in-tree TF-M SPE -> tfm_s.bin
        build("km4", args.km4_build, km4_targets)

    km0_boot = read(os.path.join(args.km0_build, KM0_BOOT))
    km0_img2 = read(os.path.join(args.km0_build, KM0_IMG2))
    km4_boot = read(os.path.join(args.km4_build, KM4_BOOT))
    km4_img2 = read(os.path.join(args.km4_build, KM4_IMG2))

    # cross-core image2 merge (imagetool.sh RSIP-off branch is a plain cat)
    km0_km4_img2 = km0_img2 + km4_img2

    print("\nassembling Image_All.bin:")
    image = bytearray()
    place(image, OFF_KM0_BOOT, km0_boot,     "km0_boot_all.bin")
    place(image, OFF_KM4_BOOT, km4_boot,     "km4_boot_all.bin")
    place(image, OFF_IMAGE2,   km0_km4_img2, "km0_km4_image2.bin")

    # TF-M boot (XIP-from-flash): tfm_s executes in place from flash 0x08100000
    # (S_CODE_START); the bootloader secure-jumps there. The driver app stays the
    # KM4 image2 (above) so the ROM image chain is intact, and is TF-M's NS side.
    if tfm_boot:
        tfm_path = os.path.join(args.km4_build, KM4_TFM_S)
        if not os.path.exists(tfm_path):
            sys.exit("AMEBAD_TFM_BOOT: %s not found.\n"
                     "  Expected the in-tree TF-M build (CMake target 'tfm_spe') to produce it.\n"
                     "  Run: cmake --build %s -t tfm_spe" % (tfm_path, args.km4_build))
        tfm = read(tfm_path)
        if OFF_IMG3 < len(image):
            sys.exit("TF-M offset 0x%x overlaps image2 (ends 0x%x)" % (OFF_IMG3, len(image)))
        place(image, OFF_IMG3, tfm, "tfm_s.bin (TF-M SPE, cached XIP @0x0E100000)")

    outdir = os.path.dirname(args.out)
    os.makedirs(outdir, exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(image)
    # also emit the merged image2 so the pieces can be flashed individually
    # (preserves the DPK/BACKUP/XIPSYS cal regions that Image_All.bin blanks).
    merged = os.path.join(outdir, "km0_km4_image2.bin")
    with open(merged, "wb") as f:
        f.write(km0_km4_img2)

    print("\n-> %s  (%d bytes)" % (args.out, len(image)))
    print("\nFlash one of two ways:")
    print("  A. single image (blanks the cal regions):")
    print("       %s  @ 0x08000000" % args.out)
    print("  B. three pieces (keeps existing cal @ 0x08001000-0x08003fff):")
    print("       %-26s @ 0x08000000" % os.path.join(args.km0_build, KM0_BOOT))
    print("       %-26s @ 0x08004000" % os.path.join(args.km4_build, KM4_BOOT))
    print("       %-26s @ 0x08006000" % merged)


if __name__ == "__main__":
    main()
