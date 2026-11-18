#!/usr/bin/env python3
"""Package the verified Green2 vendor library and NAN function wrappers.

The result is linked whole-archive with --wrap for the patched functions. The
peer object's NDP-cleanup and final-free relocations are redirected; all other vendor members
remain byte-identical. The original input archive is never modified.
Use the SDK's ASDK 12.3.1 toolchain to reproduce the checked-in archive.
"""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

BASE_SHA256 = "682488343c85376be24be761e3d8f500425c3a9787557003cade520be1cfbcaa"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cross-compile", default="arm-none-eabi-")
    parser.add_argument("archive", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    source = args.archive.resolve()
    output = args.output.resolve()
    if source == output:
        parser.error("output must differ from the original vendor archive")
    if hashlib.sha256(source.read_bytes()).hexdigest() != BASE_SHA256:
        parser.error("unverified vendor archive; refusing private ABI patch")
    srcdir = Path(__file__).resolve().parent
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="green2-nan-lib-") as tmp:
        tmp = Path(tmp)
        cipher = tmp / "whc_dev_nan_pasn_cipher_fix.o"
        auth_peer = tmp / "whc_dev_nan_auth_peer_fix.o"
        faw = tmp / "nan_faw_id_fix.o"
        bootstrap = tmp / "nan_bootstrap_fix.o"
        bootstrap_gate = tmp / "whc_dev_nan_bootstrap_fix.o"
        peer = tmp / "rtw_nan_peer.o"
        subprocess.run([
            sys.executable, str(srcdir / "make_nan_peer_free_fix.py"),
            args.cross_compile + "ar", str(source), str(peer),
        ], check=True)
        subprocess.run([
            args.cross_compile + "gcc", "-std=c11", "-Os", "-g0",
            "-mthumb", "-march=armv8.1-m.main", "-mcmse",
            "-fno-builtin", "-fno-strict-aliasing", "-fno-strict-overflow",
            "-fno-delete-null-pointer-checks", "-fno-tree-loop-distribute-patterns",
            "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra", "-Werror",
            "-c", "whc_dev_nan_pasn_cipher_fix.c", "-o", str(cipher),
        ], cwd=srcdir, check=True)
        subprocess.run([
            args.cross_compile + "gcc", "-std=c11", "-Os", "-g0",
            "-mthumb", "-march=armv8.1-m.main", "-mcmse",
            "-fno-builtin", "-ffunction-sections", "-fdata-sections",
            "-Wall", "-Wextra", "-Werror", "-c",
            "whc_dev_nan_auth_peer_fix.c", "-o", str(auth_peer),
        ], cwd=srcdir, check=True)
        subprocess.run([
            sys.executable, str(srcdir / "make_nan_faw_id_fix.py"),
            args.cross_compile + "ar", args.cross_compile + "objcopy",
            str(source), str(faw),
        ], check=True)
        subprocess.run([
            args.cross_compile + "gcc", "-std=c11", "-Os", "-g0",
            "-mthumb", "-march=armv8.1-m.main", "-mcmse",
            "-fno-builtin", "-ffunction-sections", "-fdata-sections",
            "-Wall", "-Wextra", "-Werror", "-c",
            "whc_dev_nan_bootstrap_fix.c", "-o", str(bootstrap_gate),
        ], cwd=srcdir, check=True)
        subprocess.run([
            sys.executable, str(srcdir / "make_nan_bootstrap_fix.py"),
            args.cross_compile + "ar", args.cross_compile + "objcopy",
            str(source), str(bootstrap),
        ], check=True)
        ndl = tmp / "nan_ndl_peer_fix.o"
        ndl_gate = tmp / "whc_dev_nan_ndl_peer_fix.o"
        subprocess.run([
            args.cross_compile + "gcc", "-std=c11", "-Os", "-g0",
            "-mthumb", "-march=armv8.1-m.main", "-mcmse",
            "-fno-builtin", "-ffunction-sections", "-fdata-sections",
            "-Wall", "-Wextra", "-Werror", "-c",
            "whc_dev_nan_ndl_peer_fix.c", "-o", str(ndl_gate),
        ], cwd=srcdir, check=True)
        subprocess.run([
            sys.executable, str(srcdir / "make_nan_ndl_peer_fix.py"),
            args.cross_compile + "ar", args.cross_compile + "objcopy",
            str(source), str(ndl),
        ], check=True)
        immutable = tmp / "whc_dev_nan_ndl_immutable_fix.o"
        subprocess.run([
            args.cross_compile + "gcc", "-std=c11", "-Os", "-g0",
            "-mthumb", "-march=armv8.1-m.main", "-mcmse",
            "-fno-builtin", "-ffunction-sections", "-fdata-sections",
            "-Wall", "-Wextra", "-Werror", "-c",
            "whc_dev_nan_ndl_immutable_fix.c", "-o", str(immutable),
        ], cwd=srcdir, check=True)
        archive = tmp / output.name
        ndc_control = tmp / "whc_dev_nan_ndc_control_fix.o"
        subprocess.run([
            args.cross_compile + "gcc", "-std=c11", "-Os", "-g0",
            "-mthumb", "-march=armv8.1-m.main", "-mcmse",
            "-fno-builtin", "-ffunction-sections", "-fdata-sections",
            "-Wall", "-Wextra", "-Werror", "-c",
            "whc_dev_nan_ndc_control_fix.c", "-o", str(ndc_control),
        ], cwd=srcdir, check=True)
        shutil.copyfile(source, archive)
        subprocess.run([args.cross_compile + "ar", "rD", str(archive),
                        str(peer), str(cipher), str(auth_peer), str(faw), str(bootstrap), str(bootstrap_gate),
                        str(ndl), str(ndl_gate), str(immutable), str(ndc_control)], check=True)
        subprocess.run([args.cross_compile + "ranlib", "-D", str(archive)], check=True)
        shutil.copyfile(archive, output)
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    output.with_suffix(".a.sha256").write_text(f"{digest}  {output.name}\n")
    print(f"{digest}  {output.name}")


if __name__ == "__main__":
    main()
