#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
nb="${TL3238_NIMBLE_ROOT:-component/nimble_config/upstream-1.10.0}"
port=component/telink_sdk_config
mkdir -p build/tl3238/tests
cc -std=gnu11 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$port/nimble_port/include" -I"$port/ble_controller" \
  -I"$nb/porting/npl/linux/include" -I"$nb/porting/nimble/include" \
  -I"$nb/nimble/include" -I"$nb/nimble/host/include" \
  -I"$nb/nimble/transport/include" -I"$nb/nimble/host/src" \
  project/tl3238/tests/cs_host_test.c "$port/nimble_port/ble_cs.c" \
  "$port/ble_controller/cs_hci.c" -o build/tl3238/tests/cs_host_test
# LSan cannot enumerate threads in the workspace's ptrace sandbox. This test
# has no allocations; ASan bounds and UBSan remain enabled.
ASAN_OPTIONS=detect_leaks=0 build/tl3238/tests/cs_host_test
