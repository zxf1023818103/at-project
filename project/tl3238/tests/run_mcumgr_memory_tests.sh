#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
nb="${TL3238_NIMBLE_ROOT:-component/nimble_config/upstream-1.10.0}"
mg=component/mcumgr_config
cb=component/tinycbor_config/tinycbor/src
out=build/tl3238/tests/mcumgr-memory
mkdir -p "$out/inc"
ln -sfn "$PWD/$cb" "$out/inc/tinycbor"
for borrow in 0 1; do
  cc -std=gnu11 -g -O1 -Wall -Wextra -Werror -Wno-unused-parameter \
    -fsanitize=address,undefined -DMCUMGR_SMP_BORROW_REQUEST="$borrow" \
    -include mcumgr_compat.h -I"$mg/include" -I"$mg" -I"$mg/mcumgr/mgmt/include" \
    -I"$mg/mcumgr/smp/include" -I"$out/inc" -I"$cb" \
    -Ibuild/tl3238/firmware/nimble_port/tinycbor/tinycbor -DCBOR_STATIC_DEFINE \
    -Icomponent/telink_sdk_config/nimble_port/include \
    -I"$nb/porting/npl/linux/include" -I"$nb/porting/nimble/include" \
    -I"$nb/nimble/include" -I"$nb/nimble/host/include" -I"$nb/nimble/transport/include" \
    project/tl3238/tests/mcumgr_memory_test.c "$mg/src/mcumgr_compat.c" \
    "$mg/mcumgr/mgmt/src/mgmt.c" "$mg/mcumgr/smp/src/smp.c" \
    "$cb/cborencoder.c" "$cb/cborencoder_close_container_checked.c" "$cb/cborparser.c" \
    -lm -o "$out/test-$borrow"
  ASAN_OPTIONS=detect_leaks=0 "$out/test-$borrow"
done
