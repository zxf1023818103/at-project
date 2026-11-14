# NimBLE 1.10.0 runtime subset

Source: Apache mynewt-nimble `nimble_1_10_0_tag`, commit
`a7a156f28954819e158b62dd613008f22f9cf73b`.

This component contains the Host, common transport/OS utilities, and the
FreeRTOS/Linux NPL headers needed by TL3238 and its host tests. It excludes the
upstream controller, platform demos and test suites. LICENSE and NOTICE are
retained. The existing `nimble_config/nimble` submodule remains available to
other platforms.

The already validated `../patches/freertos-platform-compat.patch` and
`../patches/cs-core60-hci.patch` are applied in these files. No network fetch or
patching occurs during a firmware build. `manifest.json` records the source
commit, patches and current file hashes. The Telink component's
`tools/verify_component.py` validates both manifests.
