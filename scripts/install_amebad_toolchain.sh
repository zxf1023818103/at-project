#!/usr/bin/env bash
# Download + install the Realtek ASDK-10.3.1 Build 4354 toolchain (arm-none-eabi)
# used by AmebaD (RTL8721D) KM0 / KM4.
#
# Default install prefix: <repo>/toolchain  (the directory is gitignored).
# Override with --prefix <DIR> or RTK_TOOLCHAIN_DIR env var, e.g. point at
# /opt/rtk-toolchain to share one install across multiple checkouts.
#
# Final layout:
#   <prefix>/asdk-10.3.1-4354/linux/newlib/bin/arm-none-eabi-gcc
#
# The matching CMake toolchain files (cmake/arm-none-eabi.cmake,
# cmake/amebad-km0.cmake) look up this exact path by default; set
# AMEBAD_TOOLCHAIN_ROOT (pointing at the newlib/ subdir) to override.
#
# Usage:
#   scripts/install_amebad_toolchain.sh                       # default = <repo>/toolchain
#   scripts/install_amebad_toolchain.sh --prefix /opt/rtk     # share across repos
#   RTK_TOOLCHAIN_DIR=/opt/rtk scripts/install_amebad_toolchain.sh

set -euo pipefail

URL="https://github.com/Ameba-AIoT/ameba-toolchain/releases/download/10.3.1_v5/asdk-10.3.1-linux-newlib-build-4354-x86_64_with_small_reent.tar.bz2"
VERSION_TAG="asdk-10.3.1-4354"

# Repo-local default: <repo>/toolchain (gitignored). Resolved from the
# script's own location so it works regardless of cwd.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
PREFIX="${RTK_TOOLCHAIN_DIR:-${REPO_ROOT}/toolchain}"

usage() {
    sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix)       PREFIX="$2"; shift 2 ;;
        --prefix=*)     PREFIX="${1#--prefix=}"; shift ;;
        -h|--help)      usage; exit 0 ;;
        *)              echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

DEST="${PREFIX}/${VERSION_TAG}"
GCC="${DEST}/linux/newlib/bin/arm-none-eabi-gcc"

# Idempotency: if a matching install already exists, exit clean.
if [[ -x "${GCC}" ]]; then
    have=$("${GCC}" --version | head -1)
    if [[ "${have}" == *"4354"* ]]; then
        echo "toolchain already installed: ${DEST}"
        echo "  ${have}"
        exit 0
    fi
    echo "warning: ${DEST} exists but version mismatch:" >&2
    echo "  found:    ${have}" >&2
    echo "  expected: build 4354" >&2
    echo "refusing to overwrite — remove ${DEST} manually and re-run" >&2
    exit 1
fi

if ! mkdir -p "${PREFIX}" 2>/dev/null; then
    echo "cannot create ${PREFIX}" >&2
    echo "re-run with sudo, or pick a writable prefix:" >&2
    echo "  $0 --prefix \$HOME/.rtk-toolchain" >&2
    exit 1
fi

TMP_DIR="$(mktemp -d -t asdk-10.3.1-XXXXXX)"
trap 'rm -rf "${TMP_DIR}"' EXIT

TARBALL="${TMP_DIR}/asdk.tar.bz2"
echo "fetching ${URL}"
if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 -C - -o "${TARBALL}" "${URL}"
elif command -v wget >/dev/null 2>&1; then
    wget -c -O "${TARBALL}" "${URL}"
else
    echo "need curl or wget on PATH" >&2; exit 1
fi

EXTRACT_DIR="${TMP_DIR}/extract"
mkdir -p "${EXTRACT_DIR}"
echo "extracting (takes ~1 min) ..."
tar -xjf "${TARBALL}" -C "${EXTRACT_DIR}"

# Tarball layout varies (some upstreams ship a single top dir, some are
# flat with "./" prefixes). Rather than guess, locate arm-none-eabi-gcc
# in the extracted tree and infer the root from its "linux/newlib/bin/"
# subpath. Everything from that root upward becomes ${DEST}.
GCC_FOUND="$(find "${EXTRACT_DIR}" -type f -name 'arm-none-eabi-gcc' 2>/dev/null | head -1)"
if [[ -z "${GCC_FOUND}" ]]; then
    echo "could not find arm-none-eabi-gcc in extracted tarball" >&2
    echo "extracted top-level entries:" >&2
    ls "${EXTRACT_DIR}" >&2
    exit 1
fi
if [[ "${GCC_FOUND}" != *"/linux/newlib/bin/arm-none-eabi-gcc" ]]; then
    echo "arm-none-eabi-gcc not under expected linux/newlib/bin/ subpath:" >&2
    echo "  ${GCC_FOUND}" >&2
    exit 1
fi
NEWLIB_TOP="${GCC_FOUND%/linux/newlib/bin/arm-none-eabi-gcc}"
echo "newlib root inside tarball: ${NEWLIB_TOP#${EXTRACT_DIR}/}"

mv "${NEWLIB_TOP}" "${DEST}"

if [[ ! -x "${GCC}" ]]; then
    echo "post-install layout unexpected — ${GCC} not executable" >&2
    echo "actual arm-none-eabi-gcc locations under ${DEST}:" >&2
    find "${DEST}" -maxdepth 5 -name 'arm-none-eabi-gcc' -type f 2>/dev/null | head -5 >&2
    exit 1
fi

echo
echo "installed: ${DEST}"
echo "  $("${GCC}" --version | head -1)"
echo
echo "use with CMake:"
echo "  cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/amebad-km0.cmake"
echo
echo "(toolchain files default to ${PREFIX}/${VERSION_TAG};"
echo " set AMEBAD_TOOLCHAIN_ROOT to override.)"
