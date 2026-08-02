#!/usr/bin/env bash
# Download + install the Zephyr SDK riscv64-zephyr-elf toolchain, required to
# build BL618DG **B0 silicon** (the mass-production part, Nuclei-900 core).
#
# Why this and not the T-Head toolchain: BL618DG ships in two flavours.
#   A-series (a0) : T-Head e907, -march=rv32imafcpzpsfoperand_xtheade, ABI ilp32f
#   B0 (volume)   : Nuclei-900,  -march=rv32imafdc_zicsr_zifencei,     ABI ilp32d
# bouffalo_sdk's project.build selects CROSS_COMPILE=riscv64-zephyr-elf- for
# CPU_MODEL=b0. The bundled T-Head GCC 10.2 cannot build it — it rejects the
# `zifencei` extension ("Invalid or unknown z ISA extension"). Note the SDK's
# b0 branch does NOT pass -mtune, so the absence of Nuclei's
# `-mtune=nuclei-900-series` in this toolchain is irrelevant.
#
# Default install prefix: <repo>/platform/toolchain_gcc_zephyr_linux
# (gitignored — 1.1 GB extracted, and upstream ships release tarballs only,
# so unlike platform/toolchain_gcc_t-head_linux it cannot be a git submodule).
# Override with --prefix <DIR> or ZEPHYR_TOOLCHAIN_DIR.
#
# Final layout:
#   <prefix>/bin/riscv64-zephyr-elf-gcc
#
# Usage:
#   scripts/install_zephyr_riscv_toolchain.sh                    # default location
#   scripts/install_zephyr_riscv_toolchain.sh --version v0.17.4  # pin an SDK release
#   scripts/install_zephyr_riscv_toolchain.sh --prefix /opt/zsdk # share across repos

set -euo pipefail

SDK_VERSION="v1.0.1"
REPO="zephyrproject-rtos/sdk-ng"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
PREFIX="${ZEPHYR_TOOLCHAIN_DIR:-${REPO_ROOT}/platform/toolchain_gcc_zephyr_linux}"

usage() {
    sed -n '2,27p' "$0" | sed 's/^# \{0,1\}//'
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix)       PREFIX="$2"; shift 2 ;;
        --prefix=*)     PREFIX="${1#--prefix=}"; shift ;;
        --version)      SDK_VERSION="$2"; shift 2 ;;
        --version=*)    SDK_VERSION="${1#--version=}"; shift ;;
        -h|--help)      usage; exit 0 ;;
        *)              echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

GCC="${PREFIX}/bin/riscv64-zephyr-elf-gcc"

# Idempotency: a working install short-circuits.
if [[ -x "${GCC}" ]]; then
    echo "toolchain already installed: ${PREFIX}"
    echo "  $("${GCC}" --version | head -1)"
    exit 0
fi

# Host arch -> asset name.
case "$(uname -m)" in
    x86_64)          HOST="linux-x86_64" ;;
    aarch64|arm64)   HOST="linux-aarch64" ;;
    *) echo "unsupported host arch: $(uname -m)" >&2; exit 1 ;;
esac
ASSET="toolchain_gnu_${HOST}_riscv64-zephyr-elf.tar.xz"

if ! mkdir -p "$(dirname "${PREFIX}")" 2>/dev/null; then
    echo "cannot create $(dirname "${PREFIX}")" >&2
    exit 1
fi

TMP_DIR="$(mktemp -d -t zsdk-riscv-XXXXXX)"
trap 'rm -rf "${TMP_DIR}"' EXIT

echo "fetching ${REPO} ${SDK_VERSION} :: ${ASSET} (~144 MB)"
if command -v gh >/dev/null 2>&1; then
    gh release download "${SDK_VERSION}" --repo "${REPO}" \
        --pattern "${ASSET}" --dir "${TMP_DIR}" --clobber
else
    URL="https://github.com/${REPO}/releases/download/${SDK_VERSION}/${ASSET}"
    if command -v curl >/dev/null 2>&1; then
        curl -fL --retry 3 -C - -o "${TMP_DIR}/${ASSET}" "${URL}"
    elif command -v wget >/dev/null 2>&1; then
        wget -c -O "${TMP_DIR}/${ASSET}" "${URL}"
    else
        echo "need gh, curl or wget on PATH" >&2; exit 1
    fi
fi

EXTRACT_DIR="${TMP_DIR}/extract"
mkdir -p "${EXTRACT_DIR}"
echo "extracting (~1.1 GB, takes a minute) ..."
tar -xJf "${TMP_DIR}/${ASSET}" -C "${EXTRACT_DIR}"

# The tarball ships a single top-level riscv64-zephyr-elf/ dir, but locate the
# compiler rather than assume, then take its bin/../ as the root.
GCC_FOUND="$(find "${EXTRACT_DIR}" -type f -name 'riscv64-zephyr-elf-gcc' 2>/dev/null | head -1)"
if [[ -z "${GCC_FOUND}" ]]; then
    echo "could not find riscv64-zephyr-elf-gcc in extracted tarball" >&2
    ls "${EXTRACT_DIR}" >&2
    exit 1
fi
TC_TOP="${GCC_FOUND%/bin/riscv64-zephyr-elf-gcc}"

mv "${TC_TOP}" "${PREFIX}"

if [[ ! -x "${GCC}" ]]; then
    echo "post-install layout unexpected — ${GCC} not executable" >&2
    exit 1
fi

echo
echo "installed: ${PREFIX}"
echo "  $("${GCC}" --version | head -1)"
echo
echo "build BL618DG B0 (mass-production silicon, dual-core AP+NP):"
echo "  export PATH=${PREFIX}/bin:\$PATH"
echo "  cd platform/bouffalo_sdk/examples/bl618dg_dualcore"
echo "  make CPU_MODEL=b0 && make whole CPU_MODEL=b0"
