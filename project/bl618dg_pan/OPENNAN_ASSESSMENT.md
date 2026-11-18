# BL618DG / OpenNAN feasibility assessment — 2026-11-01

## Conclusion

Assessed project: [seemoo-lab/OpenNAN](https://github.com/seemoo-lab/opennan),
commit `27b07063ed7fea02fd96246d592c8c30e1393aa6` fetched for this review.
SDK: the project's pinned Bouffalo `237ed937856bf435f4f3642ea4728294723e1d87`.

**Target clarification: iOS interoperability.** The relevant target is Apple's
Wi-Fi Aware connection flow, not just generic NAN discovery. See the iOS
assessment below; current OpenNAN is insufficient for that target.

**Discovery/control-plane port: conditionally feasible; an on-board radio I/O
proof of concept is required. Running the Linux daemon unchanged: no. A complete
Android-interoperable NAN IP link: not provided by the inspected OpenNAN code
and not demonstrated by the current SDK.**

This is a source/API assessment. No NAN firmware was built or flashed, no NAN
packets were transmitted, and no phone interoperability was tested. The board's
existing Wi-Fi-only throughput firmware was left unchanged.

## OpenNAN implementation boundaries

The upstream [README](https://github.com/seemoo-lab/opennan/tree/27b07063ed7fea02fd96246d592c8c30e1393aa6)
requires active monitor mode with frame injection. Its Linux daemon uses
libpcap for RX/TX, libev for scheduling, libnl/nl80211 for interface control,
and a TAP interface. These OS integrations must be replaced for FreeRTOS;
porting the C protocol library does not require making BL618DG run Linux.

The protocol code also uses Ethernet/IPv6 types, endian helpers, dynamic lists
and a monotonic clock. Radiotap adaptation is needed: the SDK does not present
Linux radiotap packets directly, so strip TX radiotap metadata and adapt RX
metadata or introduce a platform-neutral frame entry point.

Most importantly, [src/data_path.c](https://github.com/seemoo-lab/opennan/blob/27b07063ed7fea02fd96246d592c8c30e1393aa6/src/data_path.c)
contains an empty `nan_data_request()` body. That file is absent from
`src/CMakeLists.txt`; the NAN state does not contain an operational NDP session
manager. There are NDP frame definitions and an attribute encoder, but these
do not implement a complete NDP handshake. The receive entry point dispatches
Beacon and Action frames; the general NAN action handler largely logs action
subtypes. The daemon's TAP TX handler ends by logging/freeing a peer-bound frame
rather than implementing a complete standard IP data path.

Consequently, discovery success must not be presented as working NAN data-path
networking or as a ready-made replacement for Classic Bluetooth PANU.

## SDK capabilities and remaining gates

| Requirement | Evidence in current BL618DG SDK | Assessment |
|---|---|---|
| Receive raw 802.11 management frames | `wifi_mgmr_sniffer_enable()` and monitor RX functions are exported by `libfhost_bl618dg_default.a`; config enables monitor support | Entry points exist; validate NAN Beacon/Public Action delivery and filtering on hardware |
| Inject raw frames | `wifi_mgmr_raw_80211_send()` exported by the same BL618DG library; `CFG_RAW_SEND_ENABLE` enabled | Validate management-frame support, header preservation, FCS handling and actual over-air TX |
| Monitor RX while injecting | Individual APIs exist, but this combination is not demonstrated by the reviewed interfaces | First go/no-go gate; raw-send success alone is insufficient |
| Channel control | Sniffer configuration specifies primary/center frequencies; `wifi_mgmr_sniffer_set_channel()` is exported | Start fixed-channel; measure switching/settling before multi-channel use |
| Discovery timing | OpenNAN uses a software clock; SDK has MCU timers and internal RX TSF fields | Need monotonic microsecond time and measured queue/airtime jitter; public monitor metadata does not expose all internal TSF fields |
| Native NAN API | No NAN-specific public API found in the reviewed FHOST/MACSW headers | Application port cannot rely on a ready-made NAN firmware service |
| Phone-compatible NDP/IP | OpenNAN NDP incomplete; current application sets `ipconfigUSE_IPv6=0` | Substantial protocol and network integration work remains |

Source anchors in the SDK:

- `components/wireless/wifi6/fhost/include/wifi_mgmr_ext.h`: sniffer configuration
  and APIs, raw-802.11 configuration and API.
- `components/wireless/wifi6/fhost/include/cfgmacsw.h`: raw-send request fields
  include completion, channel, reservation duration and optional low-level TX
  controls. The simple public raw-send structure exposes fewer controls.
- `components/wireless/macsw/inc/macsw_config.h`: monitor/raw-send config.
- `components/wireless/macsw/inc/macsw.h`: RX vector includes TSF low/high;
  this does not mean the public monitor callback supplies those timestamps.

The default raw-send reservation duration is documented as 20 ms in internal
request structures. OpenNAN's implementation uses a discovery window of 16 TU
(16.384 ms), with an interval of 512 TU (524.288 ms). A reservation duration
is not necessarily a mandatory send delay, but its interaction with RX and
timers must be measured; API names alone cannot establish timing suitability.

OpenNAN synchronizes a software time base using received Beacon information
and `CLOCK_MONOTONIC`. Its RX code currently does not use the radiotap TSF
timestamp for `now_usec`. Thus writable hardware TSF must not be incorrectly
declared a mandatory prerequisite for porting this particular implementation.
Hardware timestamps may still improve accuracy.

No standalone RAM/Flash footprint or CPU load was measured for OpenNAN. The C
core offers a plausible porting route, but capacity must be verified with bounded
peer/service/frame pools and an actual build rather than estimated from source
file counts.

## Staged implementation proposal

1. **Radio I/O proof:** dedicated Wi-Fi-only test, GPIO16 low, no STA connection,
   no BT, fixed channel 6. Capture NAN Beacon/Public Action frames from a known
   peer. Inject this board's own valid discovery frames and confirm them on an
   independent monitor receiver. Measure frame preservation, RX continuity and
   TX latency/jitter. Failure here requires a supported lower-level MAC hook or
   vendor assistance before a full port is justified.
2. **Discovery port:** place adapters under `component/opennan_config`, with a
   separate example such as `project/bl618dg_nan`. Replace daemon I/O/event
   scheduling with an RX queue, one protocol worker and timer notifications.
   Use a microsecond monotonic clock; keep interrupt callbacks short. Preserve
   the portable cluster/sync/service code, adding platform type/endian helpers.
   Validate cluster join/merge, publish/subscribe and follow-up exchange with
   an Android Wi-Fi Aware app or a known OpenNAN peer.
3. **Data-path decision:** if the actual goal is sockets rather than discovery,
   separately implement and validate NDP negotiation, data-interface addressing,
   peer/security state and the RX/TX data path. Android's documented socket flow
   uses the peer IPv6 address; enable/integrate IPv6 in FreeRTOS+TCP and replace
   TAP with a real network interface. Do not substitute a private raw-frame
   protocol and claim standard Android NDP interoperability.
4. **Concurrency later:** only after standalone NAN works, evaluate STA/channel
   scheduling and BT coexistence. Existing Wi-Fi/BT concurrent RX faults make
   these additional test objectives, not assumptions for the first NAN port.

Android discovery and socket behavior is documented in the official
[Wi-Fi Aware guide](https://developer.android.com/develop/connectivity/wifi/wifi-aware).
Whether a particular phone exposes Wi-Fi Aware must be checked on that phone;
ordinary Wi-Fi support is insufficient evidence.

**Recommended scope:** feasibility currently supports a discovery/control-plane
proof of concept. Treat standard phone-compatible NAN networking
as a separate implementation effort, contingent on the radio I/O gate and
completion of protocol functionality missing upstream.

## iOS interoperability assessment

Apple's public Wi-Fi Aware API starts with iOS 26. Its documented supported
iPhone family starts with iPhone 12; check the actual phone's capabilities.
The app uses WiFiAware with AccessorySetupKit or DeviceDiscoveryUI for pairing,
then Network framework connections. The App ID/provisioning must carry the
`com.apple.developer.wifi-aware` capability; a device firmware change cannot
supply that app capability. See [Apple's Wi-Fi API overview](https://developer.apple.com/documentation/technotes/tn3111-ios-wifi-api-overview)
and [paired-device connection flow](https://developer.apple.com/documentation/wifiaware/connecting-paired-devices).

The official [Accessory Design Guidelines](https://developer.apple.com/accessories/Accessory-Design-Guidelines.pdf)
reviewed here are Release R32, dated 2026-10-23, chapter 55. They require
Wi-Fi Aware 4.0, pairing with identity-key caching, protected unicast/group
traffic and protected management/beacon traffic. The publisher flow uses a
six-digit PIN display method, identity-resolution attributes and secure key
state. An open, unpaired NAN data path is not the iOS compatibility target.

Chapter 55.6 also specifies deriving the peer's IPv6 link-local address from
its MAC using the standard's mapping and installing the mapping directly;
ordinary IPv6 Neighbor Discovery is not used on this path. Do not confuse
that protocol's NDP abbreviation with NAN Data Path negotiation, which is
still needed.

For this BL618DG port, those requirements imply the following engineering work:

- OpenNAN's existing discovery library can provide reference frame handling,
  but its empty data-path request and absence of an operational pairing engine
  leave the principal iOS connection machinery unimplemented.
- Generic PASN-related source exists in the SDK's upstream WPA tree. That is
  not evidence of integrated NAN 4.0 pairing, identity caching or a usable NAN
  data interface. WPA3/PMF support for ordinary STA/AP likewise does not prove
  iOS-compatible NAN security.
- Validate peer/key-table control, protected-frame TX/RX and data-interface
  handling in addition to monitor/injection timing. If necessary hooks are
  unavailable in the binary MAC implementation, vendor support is a gate.
- Enable IPv6 in FreeRTOS+TCP and provide the required peer-address/cache
  integration; the current PAN application is IPv4-only. There is no need to
  introduce lwIP solely for this work.

A practical sequence is BL618DG as Publisher and iPhone app as Subscriber:
discover the declared service, complete PIN pairing, retain pairing state,
establish a protected NAN data path, then verify IPv6 UDP echo and TCP transfer.
Discovery by the iOS picker is only the first milestone. Reconnection after
reset and app/device availability changes need their own tests.

[Espressif's official ESP-to-iPhone demonstration](https://developer.espressif.com/blog/2026/08/wifi-aware-esp-to-iphone/)
shows that an MCU-class implementation is possible, using a driver/component
with pairing and secure data-path support. It is not proof that BL618DG exposes
the same capabilities, nor a portable drop-in driver for it.

**Revised recommendation for iOS:** scope this as implementation and validation
of an iOS-compatible Wi-Fi Aware 4.0 stack, contingent on MAC/firmware hooks.
Do not estimate it as a small OpenNAN platform-layer port. No iPhone pairing
or NAN data-path tests have been performed on BL618DG.

Wi-Fi Aware supplies a local app/device connection. If the original PANU goal
remains obtaining Internet access through an iPhone, a successful NAN socket
connection does not itself provide iOS Personal Hotspot routing or NAT.
