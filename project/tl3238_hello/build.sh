#!/usr/bin/env bash
set -euo pipefail
APP_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd -- "$APP_DIR/../.." && pwd)"
# A fresh directory avoids the old vendor-CMake cache in build/tl3238_hello/firmware.
WORK_DIR="${TL3238_HELLO_BUILD_DIR:-$REPO_DIR/build/tl3238_hello/component-firmware}"
source "$REPO_DIR/component/telink_sdk/tools/toolchain.sh"
cmake -S "$APP_DIR" -B "$WORK_DIR" -G Ninja \
    -DTL3238_TOOLCHAIN="$TL3238_TOOLCHAIN" "$@"
cmake --build "$WORK_DIR" --parallel
sha256sum "$WORK_DIR/tl3238_hello.bin"
