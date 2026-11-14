#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
ras=component/nimble_config/ras
mkdir -p build/tl3238/tests
cc -std=gnu11 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$ras" project/tl3238/tests/ras_test.c \
  "$ras/ras_codec.c" "$ras/ras_control.c" "$ras/cs_distance.c" "$ras/ras_selftest.c" \
  -lm -o build/tl3238/tests/ras_test
ASAN_OPTIONS=detect_leaks=0 build/tl3238/tests/ras_test
nb="${TL3238_NIMBLE_ROOT:-component/nimble_config/upstream-1.10.0}"
cc -std=gnu11 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$ras" -Icomponent/telink_sdk_config/nimble_port/include \
  -I"$nb/porting/npl/linux/include" -I"$nb/porting/nimble/include" \
  -I"$nb/nimble/include" -I"$nb/nimble/host/include" -I"$nb/nimble/transport/include" \
  project/tl3238/tests/ras_gatt_test.c "$ras/ras_codec.c" "$ras/ras_control.c" \
  "$ras/cs_distance.c" "$ras/ras_selftest.c" -lm -o build/tl3238/tests/ras_gatt_test
ASAN_OPTIONS=detect_leaks=0 build/tl3238/tests/ras_gatt_test
