#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
base=component/ble_coc_config
out="${BLE_COC_TEST_BUILD_DIR:-build/ble-coc-tests}"
mkdir -p "$out"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$base/include" "$base/tests/coc_stream_test.c" "$base/coc_stream.c" \
    -o "$out/coc_stream_test"
ASAN_OPTIONS=detect_leaks=0 "$out/coc_stream_test"
roots=(component/nimble_config/nimble)
if [[ -n "${BLE_COC_NIMBLE_ROOT:-}" ]]; then
    roots=("$BLE_COC_NIMBLE_ROOT")
elif [[ -d component/nimble_config/upstream-1.10.0 ]]; then
    roots+=(component/nimble_config/upstream-1.10.0)
fi
for nb in "${roots[@]}"; do
    for profile in small large; do
        flags=()
        if [[ "$profile" == large ]]; then
            flags=(-DBLE_COC_RX_BUFFERS=8 -DBLE_COC_TX_QUEUE_LIMIT=8 \
                -DBLE_COC_TX_SDU_MAX=1024 -DBLE_COC_MSYS_RESERVE=12 \
                -DMYNEWT_VAL_MSYS_1_BLOCK_COUNT=64)
        fi
        printf 'NimBLE %s / %s profile\n' "$nb" "$profile"
        cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
            -fsanitize=address,undefined -ffunction-sections -fdata-sections -Wl,--gc-sections \
            "${flags[@]}" -I"$base/tests/include" -I"$base/include" -I"$base/nimble" \
            -I"$nb/porting/npl/linux/include" -I"$nb/porting/nimble/include" \
            -I"$nb/nimble/include" -I"$nb/nimble/host/include" -I"$nb/nimble/host/src" \
            -I"$nb/nimble/transport/include" \
            "$base/tests/coc_nimble_test.c" "$base/coc_stream.c" \
            "$nb/porting/nimble/src/os_mbuf.c" "$nb/porting/nimble/src/os_mempool.c" \
            "$nb/porting/nimble/src/os_msys.c" "$nb/porting/nimble/src/mem.c" \
            -o "$out/coc_nimble_${profile}_test"
        ASAN_OPTIONS=detect_leaks=0 "$out/coc_nimble_${profile}_test"
    done
done
