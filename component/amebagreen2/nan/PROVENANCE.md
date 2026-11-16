# Source provenance

## Realtek radio and IPC

Imported from the local `ameba-rtos` checkout at
`96129dc4a88ea090cad89057f01251fad8dbcc8a` (`fix/green2-nan-ios-interop`), whose
upstream is <https://github.com/Ameba-AIoT/ameba-rtos>.

`vendor/` keeps the matching Wi-Fi/WHC headers, IPC host sources, task sizes,
Wi-Fi configuration and crystal-tracking configuration. The local port adjusts
the host/NP compile configuration, FreeRTOS IPC event dispatch and allocation
requirements. Existing source license notices remain in place; Realtek
proprietary files and binary archives retain their original vendor terms.

The NAN-only branch of `vendor/ameba_wificfg.c` is locally tuned for bulk TCP:
40 AP / 32 NP packet buffers, RX aggregation 16 and TX aggregation 32. These
are source configuration changes; the imported radio archives remain intact.

`vendor/wifi_feature_dis_wtn.c` is byte-identical to that checkout's
`component/wifi/rtk_app/wifi_feature_disable/wifi_feature_dis_wtn.c`. The NAN
profile selects it instead of the older non-NAN profile's stubs: the matching
version supplies `rtw_macid_wtn_mgt_init`, the updated pending-queue hooks, and
boolean return values for the WTN IPC handlers. With R-Mesh disabled its MACID
initializer must leave the NAN allocation intact. Omitting this stub links the
archive's R-Mesh initializer, which replaces the NAN capacity with two slots
(`wtn_max_refugee_num + 2`) and produces `staadd fail` for NAN peer IDs.

Binary inputs under `component/soc/amebagreen2/project/`:

| Local file | Original directory and filename |
| --- | --- |
| `lib_wifi_whc_np.a` | `project_km4ns/lib/application/lib_wifi_whc_np.a` |
| `lib_wifi_fw.a` | `project_km4ns/lib/application/lib_wifi_fw.a` |
| `lib_coex.a` | `project_km4ns/lib/application/lib_coex.a` |
| `lib_wifi_common_km4ns.a` | `project_km4ns/lib/application/lib_wifi_common.a` |
| `lib_wifi_common_km4tz.a` | `project_km4tz/lib/application/lib_wifi_common.a` |
| `lib_whc_ap.a` | `project_km4tz/lib/application/lib_whc_ap.a` |
| `lib_wifi_rtk_app.a` | `project_km4tz/lib/application/lib_wifi_rtk_app.a` |
| `lib_wpa_lite.a` | `project_km4tz/lib/application/lib_wpa_lite.a` |

All input hashes are in [vendor/SHA256SUMS](vendor/SHA256SUMS).
`fixes/make_nan_fixed_library.py` pins the complete NP archive to
`682488343c85376be24be761e3d8f500425c3a9787557003cade520be1cfbcaa`.
The extraction scripts additionally pin the objects whose private ABI they
patch. Original archives are never modified; a derived archive is built under
the CMake output directory. The fixes originate from the reference NAN work in
`ameba-rtos`; NP adaptation and IPC forwarding are local to this port.

`fixes/whc_dev_nan_auth_peer_fix.c` adapts `GREEN2_NAN_AUTH_PEER_FIX` from
`component/wifi/whc/whc_dev/whc_dev_nan_startup_trace.c` in that same checkout
(source SHA-256 `4e07c3eb017f157e3f06d8028eb18882bc88cc884ea944cee1f39435a954473f`).
The reference installs a management-dispatch hook; this port wraps the
external `rtw_nan_pasn_rx_checking` call already gated on NAN interface 2.
Both create an ordinary unauthenticated peer when Auth1 precedes Subscribe,
then run the original PASN handler. Local guards additionally check the
supplied source address, fragmentation, zero addresses and IE bounds before
allocating. The port does not import the reference's receive tracing hooks.

## hostap

`hostap/` is the C/header dependency closure needed for PASN and SAE, from
<https://github.com/zephyrproject-rtos/hostap> commit
`1ce844ea5a9d8860d8690a125925fcc7274461df`. Files retain their copyright notices
and BSD terms; see [hostap/COPYING](hostap/COPYING) and
[hostap/README](hostap/README). `hostap/CMakeLists.txt` is local build glue.

`pasn_crypto.c`, `pasn_freertos.c`, `pasn_endian.h` and `nan_pasn.c` adapt the
required interfaces to mbedTLS and FreeRTOS. `namespace_hostap.py` prefixes
defined hostap globals in the derived archive so they cannot collide with the
vendor WPA implementation. `utils/includes.h` omits the POSIX socket headers
that are unavailable on the firmware; the other 89 imported source/header and
license files are byte-identical to that revision.

## TCP throughput protocol

`nan_tcp_protocol.c` is a local, bounded-memory C implementation of the
`NAN-THROUGHPUT/1` wire protocol used by Flash Studio's `NANThroughput.swift`
and the reference checkout's `component/wifi/whc/nan_test/rpi/nan_echo.py`.
The Python server is a protocol reference only; it is neither linked into the
firmware nor required to run a test. The FreeRTOS socket adapter and native
stream tests are local code.

## Green2 P-256 acceleration

`nan_p256.c` uses the ROM `pke_ecp_*` interfaces declared in the vendored
`ameba_pke.h`. Little-endian scalar/coordinate handling follows the vendor's
`component/ssl/mbedtls-3.6.5/hw_alt/ecc_alt.c` in the SDK. The bounded P-256
adapter, mbedTLS input/output validation and startup software/hardware
cross-checks are local; no replacement cryptographic arithmetic is implemented.
