#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
nb="${TL3238_NIMBLE_ROOT:-component/nimble_config/upstream-1.10.0}"
port=component/telink_sdk_config
lfs=component/littlefs_config/littlefs
store=component/nimble_config/store
mb=component/mbedtls_config/mbedtls
mkdir -p build/tl3238/tests
cc -std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter \
  -ffunction-sections -fdata-sections -Wl,--gc-sections -fsanitize=address,undefined \
  -DTL3238_ENABLE_CS=1 -DLFS_THREADSAFE -DLFS_NO_MALLOC -DLFS_NAME_MAX=32 \
  -DLFS_NO_DEBUG -DLFS_NO_WARN -DLFS_NO_ERROR \
  -DMBEDTLS_CONFIG_FILE='"ota_mbedtls_config.h"' \
  -I"$port/nimble_port/include" -I"$nb/porting/npl/linux/include" \
  -I"$nb/porting/nimble/include" -I"$nb/nimble/include" \
  -I"$nb/nimble/host/include" -I"$nb/nimble/host/src" -I"$nb/nimble/host/services/gatt/include" \
  -I"$nb/nimble/transport/include" -I"$lfs" -Icomponent/ble_store_config -I"$store" -I"$port/storage" \
  -I"$port/ota" -I"$mb/include" -Iproject/tl3238/tests \
  project/tl3238/tests/lfs_store_test.c "$nb/nimble/host/src/ble_store.c" \
  component/ble_store_config/lfs_blob.c component/ble_store_config/ble_bond_store.c "$store/ble_store_littlefs.c" "$port/storage/lfs_storage.c" \
  "$port/nimble_port/privacy.c" \
  "$lfs/lfs.c" "$lfs/lfs_util.c" "$port/ota/ota_storage.c" \
  "$mb/library/sha256.c" "$mb/library/aes.c" "$mb/library/platform_util.c" \
  -o build/tl3238/tests/ble_store_test
ASAN_OPTIONS=detect_leaks=0 build/tl3238/tests/ble_store_test
