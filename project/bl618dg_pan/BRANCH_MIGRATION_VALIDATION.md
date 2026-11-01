# Branch migration validation — 2026-11-01

Destination: `amebad-aws-ota` (the existing branch; no `ameba-aws-ota` branch existed).
Validation was completed before committing the migration. `master` remains at
`f16be9dc6af03ca2867a7c9c775bcfc78589bd3a`. The original stash is retained.

## Isolation

- Existing `platform/bouffalo_sdk` remains at target-branch revision `498fa78421673ba324d4e7422a02cddd9629169f`, using the original fork URL.
- New `platform/bouffalo_sdk_bl618dg` is pinned to official revision `237ed937856bf435f4f3642ea4728294723e1d87`. It stores independent Git objects, without sharing an alternates directory with the existing SDK.
- Only this dedicated SDK receives the reproducible `component/bouffalo_wifi_tcp_config/sdk-net-al.patch`. Patch content matches the pre-migration SDK edits.
- `component/bluedroid_tcp_config` owns the standalone TCP build setup and reuses the existing FreeRTOS+TCP submodule at `f755b4ff3097bb320dff72ea0ec3ace0338839d6`.
- Shared `component/freertos-plus-tcp_config`, `component/CMakeLists.txt`, toolchain CMake files, and existing Ameba/Bouffalo project source trees have no diff against destination HEAD.
- Ameba SDK revisions remain `4b964de6cbaa08c95d37ff3c1d52805e69f430d3` and `346f62cbb5cdc8badcf212bb404d232a1e55e60a`.
- Pre-existing local edits in AmebaD, FreeRTOS-Kernel, OTA and zlib submodules, and the unrelated LVGL directory, are retained. They are not staged with this migration.
- Destination ignore rules for AWS credentials and confidential Realtek documents are preserved, with the requested build-artifact rules appended.

## Build regression

All commands exited zero and generated the expected image/ELF. Existing compiler warnings remain.

| Configuration | Command |
|---|---|
| BL602 | `make -C project/bouffalo CHIP=bl602 BUILD_DIR=/tmp/branch-migrate-bl602` |
| BL616 | `make -C project/bouffalo CHIP=bl616 BUILD_DIR=/tmp/branch-migrate-bl616` |
| BL616CL | `make -C project/bouffalo CHIP=bl616cl BUILD_DIR=/tmp/branch-migrate-bl616cl` |
| AmebaD default | `make -C project/amebad` |
| AmebaD AWS OTA | `make -C project/amebad AWSOTA=ON` |
| BL618DG SPP | `make -C project/bl618dg_pan whole SPP=ON OUT=/tmp/bl618dg-migrated-build -j6` |
| BL618DG combo | `make -C project/bl618dg_pan COEX=ON COEX_RF=combo OUT=/tmp/bl618dg-migrated-combo-build -j6` |

Other Ameba and Bouffalo boards were not flashed or subjected to hardware runtime tests.
The combo result is build-only and does not resolve or qualify the previously recorded coexistence behavior.

## BL618DG runtime

The migrated SPP image was flashed through COM15 with COM23 reset control;
flash verification succeeded. Wi-Fi was not initialized. Each load ran for
30 seconds with independent receiver counting and the modulo-251 payload check.

| Test | Receiver Mbps | Bytes | Result |
|---|---:|---:|---|
| spp-tx.json | 0.541287 | 2,051,280 | matching counts, zero payload errors |
| spp-rx.json | 1.070885 | 4,096,000 | matching counts, zero payload errors |
| spp-tx-idle.json | 0.591136 | 2,258,190 | matching counts, zero payload errors |

The first TX run overlapped Git repacking. The idle-host repeat is recorded
separately. Both TX results are below the earlier 0.690–0.790 Mbps session;
this functional migration check does not establish unchanged radio throughput.
RX also differs from the earlier session (1.146 Mbps). A controlled throughput
comparison would require matched radio/host conditions. Existing connection
setup HCI continuation and TX congestion diagnostics remain recorded.

Image SHA256: `1a43742eaeadcc6c3deeed5dd81e8594f17c741fd6fed44428967588313c939c`.

Logs, per-run JSON, decoded UART data, and manifest are in
`build/validation/migration/` (ignored). The previous staged/untracked source
snapshot and SDK patch are retained locally in `/tmp/bl618dg-branch-migration/`.
The board remains on the migrated SPP image.
