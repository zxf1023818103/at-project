#!/usr/bin/env bash
set -euo pipefail
APP_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd -- "$APP_DIR/../.." && pwd)"
WORK_DIR="${TL3238_BUILD_DIR:-$REPO_DIR/build/tl3238_bluedroid}"
source "$REPO_DIR/component/telink_sdk/tools/toolchain.sh"
cmake -S "$APP_DIR" -B "$WORK_DIR/firmware" -G Ninja \
    -DTL3238_TOOLCHAIN="$TL3238_TOOLCHAIN" "$@"
cmake --build "$WORK_DIR/firmware" --parallel
sha256sum "$WORK_DIR/firmware/tl3238_bluedroid.bin"
