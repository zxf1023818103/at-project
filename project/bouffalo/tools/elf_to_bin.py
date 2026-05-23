#!/usr/bin/env python3
"""Standalone ELF -> whole_flash_data.bin pipeline.

Does not depend on platform/bouffalo_sdk/ at all. Uses bundled tools
under project/bouffalo/tools/ to:

  1. objcopy -Obinary <elf> <raw.bin>            (toolchain in PATH)
  2. (optional, --with-cfi) generate DWARF CFI from ELF + append to raw.bin
  3. bflb_fw_post_proc                             — bootheader, .hash/.ota/.xz,
                                                    copy boot2/mfg/partition/efusedata
                                                    into <build-dir>/build_out/
  4. compose flash_prog_cfg.ini from board's partition_cfg_*.toml
  5. BLFlashCommand --build                        — pack to whole_flash_data.bin
  6. pad to 4 MB

The companion build_whole.py is the SDK-coupled variant kept for the existing
Makefile flow; this script is the SDK-detached equivalent (input is just the
ELF; output is a byte-identical whole_flash_data.bin).
"""

import argparse
import platform
import shutil
import subprocess
import sys
import tomllib
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent

PLATFORM_SUFFIX = {
    "Linux": "-ubuntu",
    "Windows": ".exe",
    "Darwin": "-macos",
}[platform.system()]

BFLB_FW_POST_PROC = TOOLS_DIR / "bflb_tools" / "bflb_fw_post_proc" / f"bflb_fw_post_proc{PLATFORM_SUFFIX}"
BLFLASH_COMMAND = TOOLS_DIR / "bflb_tools" / "bouffalo_flash_cube" / f"BLFlashCommand{PLATFORM_SUFFIX}"
UNWIND_PY = TOOLS_DIR / "byai" / "unwind_6byte_table.py"
MULTI_BINS_PY = TOOLS_DIR / "byai" / "multi_bins.py"

FLASH_SIZE = 4 * 1024 * 1024


def _find_objcopy() -> str:
    objcopy = shutil.which("riscv64-unknown-elf-objcopy")
    if not objcopy:
        sys.exit("riscv64-unknown-elf-objcopy not in PATH (toolchain not active)")
    return objcopy


def _run(cmd, cwd: Path | None = None) -> None:
    print("+", " ".join(str(x) for x in cmd) + (f"  (cwd={cwd})" if cwd else ""), file=sys.stderr)
    subprocess.run(cmd, check=True, cwd=cwd)


def step_objcopy(elf: Path, raw_bin: Path) -> None:
    _run([_find_objcopy(), "-Obinary", str(elf), str(raw_bin)])


def step_append_cfi(elf: Path, raw_bin: Path) -> None:
    """Generate DWARF CFI binary and append to raw_bin under the 'DWARFCFI' tag.

    Mirrors the SDK's bflb_flash.cmake CONFIG_BACKTRACE post_build step:
        python3 unwind_6byte_table.py <out>/dwarfcfi.bin <elf>
        python3 multi_bins.py <raw.bin> --append DWARFCFI <out>/dwarfcfi.bin --output <raw.bin> --align 0x1000
    """
    dwarfcfi = raw_bin.parent / "dwarfcfi.bin"
    _run(["python3", str(UNWIND_PY), str(dwarfcfi), str(elf)])
    _run([
        "python3", str(MULTI_BINS_PY),
        str(raw_bin),
        "--append", "DWARFCFI", str(dwarfcfi),
        "--output", str(raw_bin),
        "--align", "0x1000",
    ])


def step_post_proc(chip: str, board_cfg: Path, raw_bin: Path) -> None:
    """Run bflb_fw_post_proc against the raw .bin.

    This adds the boot header, emits .hash / .ota / .xz / .xz.hash / .xz.ota,
    and copies boot2_*.bin / mfg_*.bin / partition.bin / efusedata*.bin into
    the same directory as raw_bin.
    """
    _run(
        [
            str(BFLB_FW_POST_PROC),
            f"--chipname={chip}",
            f"--imgfile={raw_bin}",
            f"--brdcfgdir={board_cfg}",
            "--appkeys=shared",
        ],
        cwd=raw_bin.parent,
    )


def step_compose_ini(chip: str, board_cfg: Path, build_dir: Path, lfs_path: Path | None) -> Path:
    """Compose flash_prog_cfg.ini under build_dir, reading partition_cfg_*.toml.

    Sections gated by pt_entry presence so the same script handles BL602/BL616
    (which have mfg) and BL616CL (which doesn't). PSM is only emitted when an
    lfs_path is given and a PSM pt_entry exists.
    """
    toml_paths = sorted(board_cfg.glob("partition_cfg_*.toml"))
    if len(toml_paths) != 1:
        sys.exit(f"expected exactly one partition_cfg_*.toml in {board_cfg}, got {toml_paths}")
    with toml_paths[0].open("rb") as f:
        pt = tomllib.load(f)

    pt_addr = pt["pt_table"]["address0"]
    entry_names = {e["name"] for e in pt.get("pt_entry", [])}
    build_out = build_dir / "build_out"

    sections = [
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
        f"filedir = ./build_out/at_{chip}.bin",
        "address = @partition",
    ]
    if "mfg" in entry_names and any(build_out.glob("mfg_*.bin")):
        sections += ["", "[mfg]", "filedir = ./build_out/mfg*.bin", "address = @partition"]
    if "PSM" in entry_names and lfs_path and lfs_path.is_file():
        sections += ["", "[PSM]", f"filedir = {lfs_path.resolve()}", "address = @partition"]

    ini = build_dir / "flash_prog_cfg.ini"
    ini.write_text("\n".join(sections) + "\n")
    return ini


def step_blflash_build(chip: str, ini: Path, build_dir: Path) -> None:
    _run(
        [
            str(BLFLASH_COMMAND),
            "--chipname", chip,
            "--config", ini.name,
            "--build",
        ],
        cwd=build_dir,
    )


def step_fetch_and_pad(chip: str, build_out: Path) -> Path:
    """BLFlashCommand writes the merged image into its own chips/<chip>/img_create/
    directory (relative to the BLFlashCommand binary), not into our build_out/.
    Pull it back into build_out and pad to 4 MB.
    """
    src = (TOOLS_DIR / "bflb_tools" / "bouffalo_flash_cube" / "chips"
           / chip / "img_create" / "whole_flash_data.bin")
    if not src.is_file():
        sys.exit(f"BLFlashCommand output missing: {src}")
    data = src.read_bytes()
    if len(data) < FLASH_SIZE:
        data += b"\xff" * (FLASH_SIZE - len(data))
    dst = build_out / "whole_flash_data.bin"
    dst.write_bytes(data)
    return dst


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Standalone ELF -> whole_flash_data.bin (no SDK dependency).",
    )
    parser.add_argument("--chip", required=True, choices=["bl602", "bl616", "bl616cl"])
    parser.add_argument("--board", required=True,
                        help="Board name; expects tools/board_configs/<board>/ to exist")
    parser.add_argument("--build-dir", type=Path, required=True,
                        help="Per-chip build dir (e.g. project/bouffalo/build_bl616)")
    parser.add_argument("--elf", type=Path,
                        help="ELF input; defaults to <build-dir>/build_out/at_<chip>.elf")
    parser.add_argument("--with-cfi", action="store_true",
                        help="Append DWARF CFI section (mirrors CONFIG_BACKTRACE)")
    parser.add_argument("--lfs-bin", type=Path,
                        help="LittleFS image to embed in PSM partition (project/lfs.bin)")
    args = parser.parse_args()

    for tool in (BFLB_FW_POST_PROC, BLFLASH_COMMAND):
        if not tool.is_file():
            sys.exit(f"required tool missing: {tool}")

    build_dir = args.build_dir.resolve()
    build_out = build_dir / "build_out"
    build_out.mkdir(parents=True, exist_ok=True)

    elf = (args.elf or build_out / f"at_{args.chip}.elf").resolve()
    if not elf.is_file():
        sys.exit(f"ELF not found: {elf}")
    raw_bin = build_out / f"at_{args.chip}.bin"

    board_cfg = TOOLS_DIR / "board_configs" / args.board
    if not board_cfg.is_dir():
        sys.exit(f"board config dir not found: {board_cfg}")

    step_objcopy(elf, raw_bin)
    if args.with_cfi:
        step_append_cfi(elf, raw_bin)
    step_post_proc(args.chip, board_cfg, raw_bin)
    ini = step_compose_ini(args.chip, board_cfg, build_dir, args.lfs_bin)
    step_blflash_build(args.chip, ini, build_dir)
    whole = step_fetch_and_pad(args.chip, build_out)
    print(f"wrote {whole}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
