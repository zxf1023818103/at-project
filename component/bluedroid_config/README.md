# BL618DG Bluedroid PANU port

ESP-IDF Bluedroid Host runs above the Bouffalo dual-mode controller. Ethernet
frames from PAN/BNEP connect to a FreeRTOS+TCP multi-interface endpoint. No lwIP
is linked. The initial integration targets BL618DG B0 and Bouffalo SDK's CMake
build; it is not yet a generic port for other targets.

Pinned sources:

- ESP-IDF: `d445e947156f276dfe0db5804e4edf6ae90fe6fb`
- Bouffalo SDK: `237ed937856bf435f4f3642ea4728294723e1d87`
- FreeRTOS+TCP: `f755b4ff3097bb320dff72ea0ec3ace0338839d6`

`bouffalo_hci.c` adapts asynchronous controller command/event/ACL traffic;
`esp_compat.c` implements the required FreeRTOS, timer, logging and hardware RNG
interfaces. `nvs_easyflash.c` persists bonding blobs through EasyFlash/LittleFS.
`pan_freertos_tcp.c` bridges Ethernet RX/TX, DHCP endpoint lifecycle, congestion
and connection generations. ESP-IDF and FreeRTOS+TCP are kept unmodified. The SDK has a small net_al
integration patch recorded in `component/bouffalo_wifi_tcp_config`.

Add the FreeRTOS+TCP component before this directory to expose the
`bluedroid_pan_tcp` target. Forward PAN callbacks to
`bluedroid_pan_tcp_event()` and register the endpoint before calling
`FreeRTOS_IPInit_Multi()`. See `project/bl618dg_pan` for a complete application.

The current image enables Classic Bluetooth and PANU only. BLE profiles,
SCO/audio, NAP, shutdown/reinitialization and sustained-load
qualification are outside this first validation. The board's shared antenna
switch must select Bluetooth: **GPIO16 high** (2.4 GHz Wi-Fi uses low). The BT-only
example holds this level throughout operation. The opt-in coexistence build
uses the upstream BSP RF/SPDT and Wi-Fi/MACSW coexistence implementation, with
`component/bouffalo_wifi_tcp_config` replacing its network adaptation layer.
Standalone RF calibration is currently blocked on the attached board; see the
validation project README before enabling it. The example uses Just Works
pairing with automatic confirmation and remains discoverable; product pairing
policy should be implemented before deployment.

The standalone validation now uses `platform/bouffalo_sdk_bl618dg` and
`component/bluedroid_tcp_config`. Existing Ameba and Bouffalo projects keep
their original SDK and shared TCP configuration.

BL616 has a standalone entry at `project/bl616_bluedroid`. The transport and
profiles are shared, while RF and CPU settings remain chip-specific. The pinned
BL616 controller capability predicate is checked before enabling Bluedroid;
this bench device reports Classic unavailable. A `bredr1` archive alone is not
proof that a particular device exposes BR/EDR.

## Standalone GATT response backpressure

For `CONFIG_PAN_GATT_BR` / `CONFIG_PAN_GATT_LE` only, `gatt_control.c` keeps up to
four server response PDUs while notifications congest ATT. The hook in
`patches/gatt-control-response.patch` preserves control responses instead of
freeing them on an already-congested send. The standalone project's
`prepare-sdk` applies the patch idempotently. Other builds do not enable it.

All queue calls run on BTU. The congestion callback only records state; the
fixture's next BTU pass flushes responses before bulk notifications. Sending
from inside the L2CAP congestion callback can reenter its scheduler and reorder
data. Reset the queue on connection teardown and validate peer/channel before
sending; accepted CONGESTED PDUs belong to L2CAP and must never be retried.

Run `python3 tests/test_gatt_control.py` from this component for host fault
injection (requires a C compiler with ASan/UBSan). Physical 60-second downlinks
passed with matching counts and zero bad bytes at 0.431 Mbps LE / 0.805 Mbps
BR/EDR on iPhone 13 Pro / iOS 27.0.1. See the independent iOS project's
[GATT control report](https://github.com/zxf1023818103/flash-studio-ios/blob/main/GATT_CONTROL_FIX.md)
for failure traces and limits. This is a fixture-specific integration, not a
claim that all Bluedroid applications/platforms have this queue wired in.

## Standalone LE L2CAP CoC

`CONFIG_PAN_BLUEDROID_COC` enables the vendored host's existing LE CoC source
manifest and compiles `ble_coc_bench.c`. The standalone BL618DG entry selects
this with `BLUEDROID_COC=ON`; other modes retain their previous configuration.
The fixture uses direct BTU-local L2CA LE CoC calls, plus a read-only GATT PSM
characteristic. It does not start PAN, Wi-Fi, or a TCP/IP stack.

The application queues TX only from its next BTU work item. It checks CoC
congestion and the separate ACL queue threshold; the test iPhone offers 65535
CoC credits, which alone permits excessive host-side buffering. The bounded
adapter reads the vendored host's private LCB queue, pauses at length 8, and
caps each submission to one K-frame. HCI fragmentation can add extra queue
entries beyond that threshold. Review `l2c_int.h` compatibility on host updates.
The API consumes submitted TX buffers even on failure, while the application
owns received SDUs and frees them after parsing.

The development server registers BTM_SEC_NONE for its PSM, uses a distinct
128-bit discovery UUID to distinguish it from NimBLE, and validates both
receiver counts and payload patterns. See `project/bl618dg_pan/README.md` and
[the iOS CoC report](https://github.com/zxf1023818103/flash-studio-ios/blob/main/BLUEDROID_L2CAP_VALIDATION.md)
for reproduction and measurements. This benchmark does not implement production
authentication or firmware authorization.
