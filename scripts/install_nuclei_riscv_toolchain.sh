#!/usr/bin/env bash
# Download + install the Nuclei RISC-V GNU toolchain (GCC 14.2.1 + newlib), the
# toolchain BL618DG **B0 silicon** (the mass-production part, Nuclei-900) builds
# with. Replaced the Zephyr SDK in commit c0e4ff1.
#
# Why this one:
#   * GCC 14 is a hard bar — the vendor macsw / wl80211 archives are slim-LTO
#     (GCC-14 bytecode, zero-byte .text), so only a same-generation GCC can read
#     them at all. The in-repo T-Head GCC 10.2 cannot, and it also rejects the
#     `zifencei` extension.
#   * Its C library is newlib, not the Zephyr SDK's picolibc. That is what the
#     switch bought: bl618dg now uses the same C library as every other platform
#     in this repo, and gets nano.specs / newlib-nano size trimming back.
#   * Its multilib set carries rv32imafdc_zaamo_zalrsc/ilp32d, exactly what those
#     vendor archives declare in Tag_RISCV_arch.
#
# What it does NOT do: its LTO support is zlib-only while the vendor bytecode is
# zstd-compressed. That is already resolved offline — the de-LTO'd archives are
# committed — so an ordinary build needs this toolchain alone. Only re-running
# scripts/delto_bl618dg_wireless.sh (i.e. when the vendor libs are updated) also
# needs the Zephyr SDK, via scripts/install_zephyr_riscv_toolchain.sh.
#
# Default install prefix: <repo>/toolchain/nuclei_riscv_newlibc_<version>
# (gitignored under /toolchain/ — 3.7 GB extracted, and upstream ships release
# tarballs only, so unlike platform/toolchain_gcc_t-head_linux it cannot be a
# submodule). That path is what cmake/riscv64-nuclei-elf.cmake looks for, so
# installing a different --version means editing _riscv_toolchain_reldir there
# to match.
#
# NOTE: the binary prefix is riscv64-unknown-elf-, the SAME as the T-Head GCC.
# The two are told apart purely by directory — never by PATH order.
#
# Usage:
#   scripts/install_nuclei_riscv_toolchain.sh                       # default location
#   scripts/install_nuclei_riscv_toolchain.sh --version 2024.06     # pin a release
#   scripts/install_nuclei_riscv_toolchain.sh --prefix /opt/nuclei  # share across repos

set -euo pipefail

TC_VERSION="2025.10"
BASE_URL="https://download.nucleisys.com/upload/files/toolchain/gcc"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
PREFIX=""

usage() {
    sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix)       PREFIX="$2"; shift 2 ;;
        --prefix=*)     PREFIX="${1#--prefix=}"; shift ;;
        --version)      TC_VERSION="$2"; shift 2 ;;
        --version=*)    TC_VERSION="${1#--version=}"; shift ;;
        -h|--help)      usage; exit 0 ;;
        *)              echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

# Resolved after arg parsing so --version picks the matching default directory.
if [[ -z "${PREFIX}" ]]; then
    PREFIX="${NUCLEI_TOOLCHAIN_DIR:-${REPO_ROOT}/toolchain/nuclei_riscv_newlibc_${TC_VERSION}}"
fi

GCC="${PREFIX}/bin/riscv64-unknown-elf-gcc"

# Idempotency: a working install short-circuits.
if [[ -x "${GCC}" ]]; then
    echo "toolchain already installed: ${PREFIX}"
    echo "  $("${GCC}" --version | head -1)"
    exit 0
fi

# Upstream publishes Linux x86-64 only (Centos/Ubuntu). No aarch64 build exists,
# so fail loudly rather than download something that cannot run.
case "$(uname -s)/$(uname -m)" in
    Linux/x86_64) ;;
    *) echo "no upstream Nuclei prebuilt for $(uname -s)/$(uname -m) (linux x86_64 only)" >&2
       exit 1 ;;
esac

ASSET="nuclei_riscv_newlibc_prebuilt_linux64_${TC_VERSION}.tar.bz2"
URL="${BASE_URL}/${ASSET}"

if ! mkdir -p "$(dirname "${PREFIX}")" 2>/dev/null; then
    echo "cannot create $(dirname "${PREFIX}")" >&2
    exit 1
fi

TMP_DIR="$(mktemp -d -t nuclei-riscv-XXXXXX)"
trap 'rm -rf "${TMP_DIR}"' EXIT

echo "fetching ${ASSET} (~895 MB)"
if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 -C - -o "${TMP_DIR}/${ASSET}" "${URL}"
elif command -v wget >/dev/null 2>&1; then
    wget -c -O "${TMP_DIR}/${ASSET}" "${URL}"
else
    echo "need curl or wget on PATH" >&2; exit 1
fi

EXTRACT_DIR="${TMP_DIR}/extract"
mkdir -p "${EXTRACT_DIR}"
echo "extracting (~3.7 GB, takes a few minutes) ..."
tar -xjf "${TMP_DIR}/${ASSET}" -C "${EXTRACT_DIR}"

# The tarball nests the toolchain a level or two down (it is packed from the
# builder's .../newlibc/<version>/gcc prefix), so locate the compiler rather than
# assume a layout, then take its bin/../ as the root.
GCC_FOUND="$(find "${EXTRACT_DIR}" -type f -name 'riscv64-unknown-elf-gcc' 2>/dev/null | head -1)"
if [[ -z "${GCC_FOUND}" ]]; then
    echo "could not find riscv64-unknown-elf-gcc in extracted tarball" >&2
    ls "${EXTRACT_DIR}" >&2
    exit 1
fi
TC_TOP="${GCC_FOUND%/bin/riscv64-unknown-elf-gcc}"

mv "${TC_TOP}" "${PREFIX}"

if [[ ! -x "${GCC}" ]]; then
    echo "post-install layout unexpected — ${GCC} not executable" >&2
    exit 1
fi

echo
echo "installed: ${PREFIX}"
echo "  $("${GCC}" --version | head -1)"
echo "  $("${GCC}" -v 2>&1 | grep -i 'LTO compression' || true)"
echo
echo "build BL618DG (B0, dual-core AP+NP — cmake finds the toolchain by path,"
echo "so no PATH export is needed):"
echo "  cd project/bl618dg && make"
