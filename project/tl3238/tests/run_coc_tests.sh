#!/usr/bin/env bash
# Compatibility entry point; shared component owns the tests.
set -euo pipefail
cd "$(dirname "$0")/../../.."
if [[ -n "${TL3238_NIMBLE_ROOT:-}" ]]; then
    export BLE_COC_NIMBLE_ROOT="$TL3238_NIMBLE_ROOT"
fi
exec bash component/ble_coc_config/tests/run_tests.sh
