#!/usr/bin/env bash
# One-shot: turn Bouffalo's slim-LTO BL618DG wireless archives into ordinary
# machine-code archives.
#
# WHY THIS EXISTS
# ---------------
# Bouffalo ships libmacsw_bl618dg_default and libmacsw_config_bl618dg_default (the
# variants this project links; the ack ones and libwl80211 are the same story)
# as *slim LTO* archives: they contain GCC-14 LTO bytecode and nothing else
# (.text is 0 bytes), so only a GCC of the same generation can consume them.
# Worse, that bytecode is **zstd-compressed**, and a GCC only reads compression
# formats it was itself configured with:
#
#   Zephyr SDK GCC 14   -> "Supported LTO compression algorithms: zlib zstd"   OK
#   Nuclei GCC 14.2.1   -> "Supported LTO compression algorithms: zlib"        FAILS
#                          ("lto1: fatal error: compiler does not support ZSTD
#                            LTO compression")
#
# The project wants the Nuclei toolchain (newlib rather than the Zephyr SDK's
# picolibc, plus nano.specs). This script bridges the two: it uses the Zephyr SDK
# ONCE to run the LTO stage to completion and emit a plain relocatable object
# (-flinker-output=nolto-rel), then re-archives it. The result has zero .gnu.lto
# sections and links with any RISC-V toolchain.
#
# Run it only when the vendor archives change; the outputs are committed, so a
# normal build needs the Nuclei toolchain alone and the Zephyr SDK can go away.
#
# Usage: scripts/delto_bl618dg_wireless.sh [--zephyr <sdk-root>]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ZEPHYR="${REPO_ROOT}/platform/toolchain_gcc_zephyr_linux"

while [ $# -gt 0 ]; do
    case "$1" in
        --zephyr)   ZEPHYR="$2"; shift 2 ;;
        --zephyr=*) ZEPHYR="${1#--zephyr=}"; shift ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

GCC="${ZEPHYR}/bin/riscv64-zephyr-elf-gcc"
AR="${ZEPHYR}/bin/riscv64-zephyr-elf-ar"
OBJCOPY="${ZEPHYR}/bin/riscv64-zephyr-elf-objcopy"

if [ ! -x "${GCC}" ]; then
    cat >&2 <<EOF
Zephyr SDK GCC not found at:
  ${GCC}

It is needed ONLY to run this conversion (it is the only GCC here that reads
zstd-compressed LTO bytecode). Install it with
scripts/install_zephyr_riscv_toolchain.sh, or point at an existing copy with
  scripts/delto_bl618dg_wireless.sh --zephyr /path/to/sdk
EOF
    exit 1
fi

# Must match cmake/bl618dg-{ap,np}-toolchain.cmake.
MARCH="rv32imafdc_zicsr_zifencei"
MABI="ilp32d"

SRC_MACSW="${REPO_ROOT}/platform/bouffalo_sdk/components/wireless/macsw/lib"
OUT="${REPO_ROOT}/component/bouffalo_sdk/bl618dg_wireless/lib"

mkdir -p "${OUT}"
TMP="$(mktemp -d -t delto-bl618dg-XXXXXX)"
trap 'rm -rf "${TMP}"' EXIT

convert() {
    local src="$1" name
    name="$(basename "${src}")"
    if [ ! -f "${src}" ]; then
        echo "  skip (absent): ${name}"
        return
    fi
    echo "  ${name}"
    # --whole-archive: partial linking has no "what is referenced" context, so
    # every member must be pulled in. Dead code is dropped later by the final
    # link's --gc-sections.
    # -ffunction-sections/-fdata-sections are ESSENTIAL, not hygiene. LTO output is
    # one big .o; without per-symbol sections the final link's --gc-sections has no
    # granularity and drags in everything, including macsw's LA-capture code and the
    # 1 MB LARAM buffer it references (RAM went to 407%). With them, the unused LA
    # path is collected and LARAM goes with it.
    "${GCC}" -r -nostdlib -flto -flinker-output=nolto-rel \
        -ffunction-sections -fdata-sections \
        -march="${MARCH}" -mabi="${MABI}" \
        -Wl,--whole-archive "${src}" -Wl,--no-whole-archive \
        -o "${TMP}/${name%.a}.o"
    # Drop DWARF: these are committed binaries and the debug info is ~10x the code
    # (30 MB -> 2 MB across the set). --strip-debug keeps .symtab, so a backtrace
    # through the WiFi stack still resolves function names -- and there are no
    # vendor sources to step through anyway.
    "${OBJCOPY}" --strip-debug "${TMP}/${name%.a}.o"
    rm -f "${OUT}/${name}"
    "${AR}" crD "${OUT}/${name}" "${TMP}/${name%.a}.o"
}

echo "de-LTO'ing BL618DG wireless archives (${MARCH}/${MABI}) -> ${OUT}"
for f in "${SRC_MACSW}"/libmacsw_bl618dg_default.a \
         "${SRC_MACSW}"/libmacsw_config_bl618dg_default.a; do
    convert "${f}"
done

echo
echo "verifying no LTO bytecode survives:"
for f in "${OUT}"/*.a; do
    n="$("${ZEPHYR}/bin/riscv64-zephyr-elf-objdump" -h "${f}" 2>/dev/null | grep -c 'gnu\.lto' || true)"
    printf '  %-44s lto-sections=%s\n' "$(basename "${f}")" "${n}"
    if [ "${n}" != "0" ]; then
        echo "ERROR: ${f} still carries LTO bytecode" >&2
        exit 1
    fi
done
echo
echo "done. These are committed; normal builds need only the Nuclei toolchain."
