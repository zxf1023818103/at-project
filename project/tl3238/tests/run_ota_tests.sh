#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
mg=component/mcumgr_config
cb=component/tinycbor_config/tinycbor/src
mb=component/mbedtls_config/mbedtls
out=build/tl3238/tests/ota
gen=build/tl3238/firmware/nimble_port
mkdir -p "$out/inc"
ln -sfn "$PWD/$cb" "$out/inc/tinycbor"
cc -std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
  -fsanitize=address,undefined -DMCUMGR_SMP_BORROW_REQUEST=1 \
  -DMBEDTLS_CONFIG_FILE='"ota_mbedtls_config.h"' -DCBOR_STATIC_DEFINE -DCBORATTR_MAX_SIZE=512 \
  -include mcumgr_compat.h -I"$mg/include" -I"$mg/mcumgr/mgmt/include" \
  -I"$mg/mcumgr/smp/include" -I"$mg/mcumgr/cmd/img_mgmt/include" \
  -I"$mg/mcumgr/cmd/img_mgmt/src" -I"$mg/mcumgr/cborattr/include" \
  -I"$mg/mcumgr/util/include" -I"$out/inc" -I"$cb" \
  -I"$gen/tinycbor/tinycbor" -I"$mb/include" -Iproject/tl3238/tests \
  -Icomponent/telink_sdk_config/ota \
  project/tl3238/tests/ota_test.c component/telink_sdk_config/ota/ota_storage.c \
  component/telink_sdk_config/ota/ota_mcumgr.c "$mb/library/sha256.c" "$mb/library/platform_util.c" \
  "$mg/src/mcumgr_compat.c" "$mg/mcumgr/mgmt/src/mgmt.c" "$mg/mcumgr/smp/src/smp.c" \
  "$gen/img_mgmt.c" "$gen/img_mgmt_state.c" "$mg/mcumgr/cmd/img_mgmt/src/img_mgmt_util.c" \
  "$gen/mcumgr/cborattr.c" "$mg/mcumgr/util/src/mcumgr_util.c" \
  "$cb/cborencoder.c" "$cb/cborencoder_close_container_checked.c" "$cb/cborparser.c" \
  -lm -o "$out/test"
ASAN_OPTIONS=detect_leaks=0 "$out/test"
