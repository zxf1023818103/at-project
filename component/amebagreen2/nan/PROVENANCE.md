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

The peer capacity is locally increased from 6 to 12 (SDK maximum 15) for
overlapping iOS identities during native peer aging. The shared NAN linker
layout gives NP 184 KiB SRAM instead of 160 KiB; it moves the AP boundary by
the same 24 KiB. Native peer liveness and authentication are unchanged; the
omitted mutex deletions are corrected as described below.
`nan_peer_monitor.c` wraps the NP's external `rtw_nan_linked_status_chk` call
for read-only heap/occupancy telemetry. In the pinned archive, the peer MACID
bitmap is a 16-bit field at `g_nanpriv + 2324` and the enabled bit is bit 1 at
offset 2350. Its log runs after the native check, every 10 seconds or on a
changed sampled occupancy. It reports current free bytes, not a true minimum.

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
The patch scripts additionally pin the objects whose private ABI they
patch. Original archives are never modified; a derived archive is built under
the CMake output directory. The fixes originate from the reference NAN work in
`ameba-rtos`; NP adaptation, IPC forwarding and peer cleanup are local to
this port.

`fixes/make_nan_peer_free_fix.py` replaces only `rtw_nan_peer.o` in the derived
archive, pinning its input to
`df936272638fdd87c30150a0b8599e522e332f4e42040201b612e358ca7cb97a`.
Native peer allocation creates three queue mutexes at offsets 20/32/44. Native
destruction drains the queues and gives their locks but deletes only the last
mutex. The script redirects the destructor's final `R_ARM_THM_JUMP24` at
offset `0x7a` from `rtos_mem_free` to `green2_nan_peer_storage_free`, which
deletes the two remaining mutexes through `rtos_mutex_delete_static` before
freeing the peer storage. This returns static pool slots as well as dynamic
fallback allocations.

The same destructor also skips `rtw_nan_ndc_info_del` when peers age out.
Its NDP cleanup then erases the NDC ID at peer offset 96 and the selected bit
at offset 176, losing the reference to a 20-byte scheduler list entry. The
script additionally redirects `R_ARM_THM_CALL` at `0x48` from
`rtw_nan_ndp_data_free` to `green2_nan_peer_ndp_free`. This calls the wrapped NDC
delete API before native NDP cleanup. Adding this call alone was insufficient:
native `rtw_nan_ndl_reply_accept_event` also increments the NDC reference on
repeated schedule acceptance, while NDP-empty handling decrements only once.

`nan_ndc_reference.c` wraps NDC add/delete and records one held NDC ID per peer
in 15 fixed slots, matching the SDK's peer-capacity ceiling. Repeated acceptance
of the same peer/ID does not acquire another reference; different peers sharing
an ID retain distinct references. A changed ID releases the recorded old ID
before acquiring the new one. Cleanup uses the recorded ID even if negotiation
has already changed or cleared the live peer's fields. A mutex created once
serializes ownership changes before entering the native list mutex. A full
owner table never adds an untracked native reference.

For release, the wrapper calls the original native delete on an aligned
180-byte cleanup view containing only the recorded ID at offset 96 and the
selected bit at offset 176. Disassembly of the pinned archive verifies those
are the only peer fields native delete reads. This preserves native list
locking and reference counting without temporarily replacing the live peer's
ID or manually editing a scheduler-list entry. The wrapper then clears the
live peer's selected bit, as successful native deletion does. Repeated delete
has no held reference to decrement. All of these private ABI assumptions are
gated by the original archive SHA-256 check in `make_nan_fixed_library.py`.

NAN stop has a different ownership boundary: `rtw_nan_schdlr_priv_free` frees
every NDC entry and deletes the native list mutex before peer queue teardown.
Its wrapper clears the owner table before that bulk destruction. Later peer
cleanup therefore performs no native NDC lookup against the deleted mutex.
The native stop sequencing and scheduler teardown remain intact; the owner
mutex is retained across stop/start.

Only these two relocations change; instruction bytes and all other relocations
are preserved. This covers same-object retirement and allocation rollback
calls that an external-symbol `--wrap` could miss. Native tests check cleanup
ordering, shared references, already-released/partial peers, mutex ownership,
partial/duplicate handles, the exact patch boundary and unknown-input rejection.
The NDC ownership test additionally covers repeated schedule acceptance,
shared clusters, changed/cleared IDs, failed allocations, table exhaustion and
stop/start with live owners, and 1,000 complete peer lifetimes with balanced
allocations/frees.

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
