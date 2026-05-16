#!/usr/bin/env python3
"""Assemble whole_flash_data.bin from partition_cfg_*.toml.

The SDK's partition_cfg_*.toml under bsp/board/<BOARD>/config/ is the single
source of truth for flash layout. This script reads it, emits a transient
flash_prog_cfg.ini under BUILD_DIR (never checked in), and invokes
BLFlashCommand --build to pack the binaries from BUILD_DIR/build_out into the
merged image. The result lands in BUILD_DIR/build_out/whole_flash_data.bin.
"""

import argparse
import platform
import subprocess
import sys
import tomllib
from pathlib import Path


# pt_entry name → ini section name. Sections we emit with @partition refer to
# the pt_table by name; BLFlashCommand looks the address up in partition.bin.
SECTION_PT_ENTRY = {"FW": "FW", "mfg": "mfg"}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chip", required=True)
    parser.add_argument("--board", required=True)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--sdk-base", required=True, type=Path)
    args = parser.parse_args()

    board_cfg_dir = args.sdk_base / "bsp/board" / args.board / "config"
    toml_paths = sorted(board_cfg_dir.glob("partition_cfg_*.toml"))
    if len(toml_paths) != 1:
        sys.exit(f"expected exactly one partition_cfg_*.toml in {board_cfg_dir}, got {toml_paths}")
    with toml_paths[0].open("rb") as f:
        pt = tomllib.load(f)

    pt_addr = pt["pt_table"]["address0"]
    entry_names = {e["name"] for e in pt.get("pt_entry", [])}

    build_dir = args.build_dir.resolve()
    build_out = build_dir / "build_out"

    # Compose ini sections. Paths are relative to BUILD_DIR (we chdir there).
    sections: list[str] = [
        "[cfg]",
        "erase = 1",
        "skip_mode = 0x0, 0x0",
        "boot2_isp_mode = 0",
        "",
        "[boot2]",
        "filedir = ./build_out/boot2_*.bin",
        "address = 0x000000",
        "",
        "[partition]",
        "filedir = ./build_out/partition*.bin",
        f"address = {pt_addr:#x}",
        "",
        "[FW]",
        f"filedir = ./build_out/at_{args.chip}.bin",
        "address = @partition",
    ]
    # Only emit [mfg] when a mfg binary actually exists (bl616cl declares an mfg
    # pt_entry but ships no mfg image — BLFlashCommand errors on missing glob).
    if "mfg" in entry_names and any(build_out.glob("mfg_*.bin")):
        sections += [
            "",
            "[mfg]",
            "filedir = ./build_out/mfg*.bin",
            "address = @partition",
        ]

    ini_path = build_dir / "flash_prog_cfg.ini"
    ini_path.write_text("\n".join(sections) + "\n")

    suffix = {"Linux": "-ubuntu", "Windows": ".exe", "Darwin": "-macos"}[platform.system()]
    flash_tool = args.sdk_base / "tools/bflb_tools/bouffalo_flash_cube" / f"BLFlashCommand{suffix}"

    # BLFlashCommand --build reads --config and emits the merged image into
    # tools/bflb_tools/bouffalo_flash_cube/chips/<chip>/img_create/.
    cmd = [
        str(flash_tool),
        "--chipname", args.chip,
        "--config", ini_path.name,
        "--build",
    ]
    print("+", " ".join(cmd), f"(cwd={build_dir})", file=sys.stderr)
    subprocess.run(cmd, check=True, cwd=build_dir)

    src = args.sdk_base / "tools/bflb_tools/bouffalo_flash_cube/chips" / args.chip / "img_create/whole_flash_data.bin"
    dst = build_out / "whole_flash_data.bin"
    dst.write_bytes(src.read_bytes())
    print(f"wrote {dst}", file=sys.stderr)


if __name__ == "__main__":
    main()
