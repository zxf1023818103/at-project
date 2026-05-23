# project/bouffalo/tools

Standalone ELF → flashable BIN pipeline. Independent of
`platform/bouffalo_sdk/`.

## Layout

```
tools/
├── elf_to_bin.py                     # main entry: ELF -> whole_flash_data.bin
├── build_whole.py                    # legacy wrapper used by the SDK Makefile
├── bflb_tools/
│   ├── bflb_fw_post_proc/            # vendor PyInstaller binary (bootheader, hash, ota, xz)
│   └── bouffalo_flash_cube/
│       ├── BLFlashCommand-ubuntu     # vendor PyInstaller binary (final merge)
│       └── chips/{bl602,bl616,bl616cl}/  # per-chip database (bootheader/partition/loader)
├── board_configs/{bl602dk,bl616dk,bl616cldk}/   # boot2, mfg, partition.toml, dts
└── byai/                             # python scripts (DWARF CFI append, multi-bin)
```

## Usage

```bash
export PATH=$AT_PROJECT_ROOT/platform/toolchain_gcc_t-head_linux/bin:$PATH

python3 elf_to_bin.py \
    --chip bl616 --board bl616dk \
    --build-dir $AT_PROJECT_ROOT/project/bouffalo/build_bl616 \
    --lfs-bin  $AT_PROJECT_ROOT/project/lfs.bin
```

The ELF defaults to `<build-dir>/build_out/at_<chip>.elf`; pass `--elf` to
override. Pass `--with-cfi` if the ELF needs the DWARF CFI section
appended (matches the SDK's `CONFIG_BACKTRACE=y` post_build step).

## Verified byte-equivalent to SDK build

| Chip    | md5(whole_flash_data.bin) |
|---------|--------------------------|
| BL602   | 3c34432a8df98f1ebf23a72c36813149 |
| BL616   | d22c5bb1952a3d762c589cf30b77e55a |
| BL616CL | 0cce1ee04dc38316195130d17a716ddb |

These md5 hold across:
  - normal SDK build:        `make -C project bl616`
  - this script:             `python3 elf_to_bin.py --chip bl616 ...`
  - this script with the entire `platform/bouffalo_sdk/` directory removed.

## Currently bundled binaries

Only the Linux x86_64 vendor binaries are checked in (`-ubuntu` suffix).
macOS / Windows variants can be copied from a SDK checkout when needed; the
script picks them by host platform.
