#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
mkdir -p build/ble-store-tests
cc -std=gnu11 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -pthread \
  -DLFS_THREADSAFE -DLFS_NO_MALLOC -DLFS_NAME_MAX=32 -DLFS_NO_DEBUG -DLFS_NO_WARN -DLFS_NO_ERROR \
  -Icomponent/ble_store_config/tests/include -Icomponent/ble_store_config \
  -Icomponent/littlefs_config/littlefs -Icomponent/bluedroid_config/include \
  -Icomponent/bluedroid_config/esp-idf/components/esp_common/include \
  component/ble_store_config/tests/nvs_test.c component/ble_store_config/lfs_blob.c \
  component/littlefs_config/littlefs/lfs.c component/littlefs_config/littlefs/lfs_util.c \
  -o build/ble-store-tests/nvs_test
ASAN_OPTIONS=detect_leaks=0 build/ble-store-tests/nvs_test
