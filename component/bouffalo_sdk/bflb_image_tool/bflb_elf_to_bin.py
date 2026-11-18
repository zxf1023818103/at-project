#!/usr/bin/env python3
"""Standalone ELF -> <name>_<chip>_whole_flash_data.bin pipeline.

Does not depend on platform/bouffalo_sdk/ at all. Uses bundled tools under
tool/ (the repo's `tool/` directory) to:

  1. stage the input ELF as <build-dir>/build_out/<name>_<chip>.elf
  2. objcopy -Obinary <elf> <raw.bin>            (toolchain in PATH)
  3. (optional, --with-cfi) generate DWARF CFI from ELF + append to raw.bin
  4. bflb_fw_post_proc                             — bootheader, .bin.hash/.bin.ota,
                                                    .xz/.xz.hash/.xz.ota,
                                                    copy boot2/mfg/partition/efusedata
                                                    into <build-dir>/build_out/
  5. compose flash_prog_cfg.ini from board's partition_cfg_*.toml
  6. BLFlashCommand --build                        — pack to
                                                    <name>_<chip>_whole_flash_data.bin
  7. pad to 4 MB

The --name argument controls the output basename (default: 'at', matching the
historical AT firmware artefact names). Build artefacts are always
<name>_<chip>.<ext> so multiple firmware projects can coexist in one build
directory.
"""

import argparse
import platform
import shutil
import subprocess
import sys
import tomllib
from pathlib import Path

DEFAULT_NAME = "at"

TOOLS_DIR = Path(__file__).resolve().parent

PLATFORM_SUFFIX = {
    "Linux": "-ubuntu",
    "Windows": ".exe",
    "Darwin": "-macos",
}[platform.system()]

BFLB_FW_POST_PROC = TOOLS_DIR / "bflb_tools" / "bflb_fw_post_proc" / f"bflb_fw_post_proc{PLATFORM_SUFFIX}"
BLFLASH_COMMAND = TOOLS_DIR / "bflb_tools" / "bouffalo_flash_cube" / f"BLFlashCommand{PLATFORM_SUFFIX}"
# Low-power firmware concat (the SDK's CONFIG_POST_BUILDS_CONCAT_WITH_LP_FW). TOOLS_DIR is
# component/bouffalo_sdk/bflb_image_tool -> repo root is three parents up.
LPFW_DIR = TOOLS_DIR.parent.parent.parent / "platform" / "bouffalo_sdk" / "tools" / "lpfw"
PATCH_LPFW = LPFW_DIR / f"patch_lpfw{PLATFORM_SUFFIX}"
UNWIND_PY = TOOLS_DIR / "byai" / "unwind_6byte_table.py"
MULTI_BINS_PY = TOOLS_DIR / "byai" / "multi_bins.py"

FLASH_SIZE = 4 * 1024 * 1024


def _resolve_objcopy(explicit: str | None) -> str:
    if explicit:
        return explicit
    found = shutil.which("riscv64-unknown-elf-objcopy")
    if not found:
        sys.exit(
            "riscv64-unknown-elf-objcopy not in PATH "
            "(toolchain not active; pass --objcopy to point at it explicitly)"
        )
    return found


def _run(cmd, cwd: Path | None = None) -> None:
    print("+", " ".join(str(x) for x in cmd) + (f"  (cwd={cwd})" if cwd else ""), file=sys.stderr)
    subprocess.run(cmd, check=True, cwd=cwd)


def step_objcopy(objcopy: str, elf: Path, raw_bin: Path) -> None:
    _run([objcopy, "-Obinary", str(elf), str(raw_bin)])


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


def step_patch_lpfw(chip: str, raw_bin: Path) -> None:
    """Concatenate the low-power (lp_fw) firmware into the FW .bin — the SDK's
    CONFIG_POST_BUILDS_CONCAT_WITH_LP_FW post-build. patch_lpfw writes the lp_fw SHA-256 and appends
    <chip>_lp_fw.bin onto the 'LPFW' placeholder the linked image reserves at its tail (from
    bl<chip>_lp_fw_image.c). Runs on the post-proc'd FW bin, BEFORE the whole-flash pack, so the
    lp_fw rides into the FW partition. patch_lpfw exits 0 even when it can't find the placeholder (it
    just prints 'maybe is a merged image ...' and leaves the bin unchanged) — so scan its output and
    fail loudly, otherwise a silently-unpatched image would ship without the low-power firmware.
    """
    lp_fw = LPFW_DIR / "bin" / f"{chip}_lp_fw.bin"
    if not PATCH_LPFW.exists():
        raise SystemExit(f"lp_fw: patch_lpfw tool missing at {PATCH_LPFW}")
    if not lp_fw.exists():
        raise SystemExit(f"lp_fw: no low-power firmware blob for {chip} at {lp_fw}")
    result = subprocess.run(
        [str(PATCH_LPFW), str(raw_bin), str(lp_fw)],
        check=True, capture_output=True, text=True,
    )
    sys.stderr.write(result.stdout)
    if "maybe is a merged image" in result.stdout:
        raise SystemExit(
            f"lp_fw: patch_lpfw found no 'LPFW' placeholder at the tail of {raw_bin.name}. The "
            f"low-power fw image object ({chip}_lp_fw_image.c) must be linked LAST in the FW image "
            f"(a dedicated linker section at the end); on this SDK bl616's lp_fw placeholder is gated "
            f"off (bl616_lp_internal.h '#if (0 && ...)'), so concat is a no-op there. Clean the build "
            f"(the bin may already be merged) or enable the placeholder before using --with-lp-fw."
        )
    print(f"[lp_fw] concatenated {lp_fw.name} ({lp_fw.stat().st_size} B) into {raw_bin.name}", file=sys.stderr)


def step_append_np(raw_bin: Path, np_bin: Path) -> None:
    """Append the NP-core image into the AP-core FW .bin — the SDK's
    CONFIG_DUALCORE_NP_IMAGE post-build (bflb_flash.cmake), which runs
    byai/multi_bins.py to add an 'NP' descriptor + payload at 0x400 alignment.

    BL618DG only. Must run AFTER step_post_proc (the header/CRC the post-proc
    writes covers the AP image) and BEFORE the whole-flash pack, so the NP
    payload rides inside the FW partition. A BL618DG AP image without its NP
    payload does not boot, so a missing file is fatal rather than skipped.
    """
    if not np_bin.is_file():
        raise SystemExit(f"dualcore: NP image not found at {np_bin} (build app_np first)")
    if not MULTI_BINS_PY.exists():
        raise SystemExit(f"dualcore: multi_bins.py missing at {MULTI_BINS_PY}")
    before = raw_bin.stat().st_size
    subprocess.run(
        [sys.executable, str(MULTI_BINS_PY), str(raw_bin),
         "--append", "NP", str(np_bin), "--output", str(raw_bin), "--align", "0x400"],
        check=True,
    )
    print(f"[dualcore] appended {np_bin.name} ({np_bin.stat().st_size} B) into "
          f"{raw_bin.name} ({before} -> {raw_bin.stat().st_size} B)", file=sys.stderr)


def step_compose_ini(chip: str, name: str, board_cfg: Path, build_dir: Path, lfs_path: Path | None) -> Path:
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

    sections = [
        "[cfg]",
        "erase = 1",
        "skip_mode = 0x0, 0x0",
        "boot2_isp_mode = 0",
        "",
        "[boot2]",
        "filedir = ./boot2_*.bin",
        "address = 0x000000",
        "",
        "[partition]",
        "filedir = ./partition*.bin",
        f"address = {pt_addr:#x}",
        "",
        "[FW]",
        f"filedir = ./{name}_{chip}.bin",
        "address = @partition",
    ]
    if "mfg" in entry_names and any(build_dir.glob("mfg_*.bin")):
        sections += ["", "[mfg]", "filedir = ./mfg*.bin", "address = @partition"]
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


def step_fetch_and_pad(chip: str, name: str, build_dir: Path) -> Path:
    """BLFlashCommand writes the merged image into its own chips/<chip>/img_create/
    directory (relative to the BLFlashCommand binary), not into our build_dir.
    Pull it back into build_dir under <name>_<chip>_whole_flash_data.bin and
    pad to 4 MB.
    """
    src = (TOOLS_DIR / "bflb_tools" / "bouffalo_flash_cube" / "chips"
           / chip / "img_create" / "whole_flash_data.bin")
    if not src.is_file():
        sys.exit(f"BLFlashCommand output missing: {src}")
    data = src.read_bytes()
    if len(data) < FLASH_SIZE:
        data += b"\xff" * (FLASH_SIZE - len(data))
    dst = build_dir / f"{name}_{chip}_whole_flash_data.bin"
    dst.write_bytes(data)
    return dst


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Standalone ELF -> whole_flash_data.bin (no SDK dependency).",
    )
    parser.add_argument("--chip", required=True, choices=["bl602", "bl616", "bl616cl", "bl618dg"])
    parser.add_argument("--board", required=True,
                        help="Board name; expects tool/board_configs/<board>/ to exist")
    parser.add_argument("--build-dir", type=Path, required=True,
                        help="Per-chip build dir (e.g. project/bouffalo/build_bl616)")
    parser.add_argument("--name", default=DEFAULT_NAME,
                        help=f"Output basename; produces <name>_<chip>.bin etc. (default: {DEFAULT_NAME})")
    parser.add_argument("--elf", type=Path,
                        help="ELF input; defaults to <build-dir>/build_out/<name>_<chip>.elf")
    parser.add_argument("--append-np", type=Path,
                        help="BL618DG only: NP-core .bin to append into the AP image "
                             "(the SDK's CONFIG_DUALCORE_NP_IMAGE post-build)")
    parser.add_argument("--with-cfi", action="store_true",
                        help="Append DWARF CFI section (mirrors CONFIG_BACKTRACE)")
    parser.add_argument("--lfs-bin", type=Path,
                        help="LittleFS image to embed in PSM partition (project/lfs.bin)")
    parser.add_argument("--with-lp-fw", action="store_true",
                        help="Concatenate the low-power firmware into the FW bin "
                             "(the SDK's CONFIG_POST_BUILDS_CONCAT_WITH_LP_FW)")
    parser.add_argument("--objcopy",
                        help="Path to riscv64-unknown-elf-objcopy (defaults to PATH lookup)")
    args = parser.parse_args()

    objcopy = _resolve_objcopy(args.objcopy)

    for tool in (BFLB_FW_POST_PROC, BLFLASH_COMMAND):
        if not tool.is_file():
            sys.exit(f"required tool missing: {tool}")

    build_dir = args.build_dir.resolve()
    build_dir.mkdir(parents=True, exist_ok=True)

    basename = f"{args.name}_{args.chip}"
    elf_target = (build_dir / f"{basename}.elf").resolve()

    if args.elf:
        elf_in = args.elf.resolve()
        if not elf_in.is_file():
            sys.exit(f"ELF not found: {elf_in}")
        # Stage the source ELF under the basename so the bundle of artefacts
        # produced by this run is self-contained.
        if elf_in != elf_target:
            shutil.copy2(elf_in, elf_target)
    elif not elf_target.is_file():
        sys.exit(f"ELF not found: {elf_target}")

    raw_bin = build_dir / f"{basename}.bin"

    board_cfg = TOOLS_DIR / "board_configs" / args.board
    if not board_cfg.is_dir():
        sys.exit(f"board config dir not found: {board_cfg}")

    step_objcopy(objcopy, elf_target, raw_bin)
    if args.with_cfi:
        step_append_cfi(elf_target, raw_bin)
    step_post_proc(args.chip, board_cfg, raw_bin)
    if args.with_lp_fw:
        step_patch_lpfw(args.chip, raw_bin)
    if args.append_np:
        step_append_np(raw_bin, args.append_np)
    ini = step_compose_ini(args.chip, args.name, board_cfg, build_dir, args.lfs_bin)
    step_blflash_build(args.chip, ini, build_dir)
    whole = step_fetch_and_pad(args.chip, args.name, build_dir)
    print(f"wrote {whole}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
