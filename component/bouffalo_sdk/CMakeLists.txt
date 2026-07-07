# bouffalo_sdk — umbrella INTERFACE library that links all chip-relevant
# Bouffalo components in one shot. Consumers do:
#
#   target_link_libraries(<exe> PRIVATE bouffalo_sdk)
#
# and pick up the low-level HAL, board init sources, linker script, and
# the -ufw_header link option transitively.

# BOUFFALO_CHIP gating happens in component/CMakeLists.txt (the file
# that add_subdirectory's this one). We can rely on it being set + valid
# by the time we get here.

# FreeRTOS-Kernel (component/freertos-kernel_config/freertos-kernel, the bouffalo-port
# branch on the zxf1023818103 fork). Configure it BEFORE add_subdirectory
# so the kernel's CMakeLists can read FREERTOS_PORT + freertos_config.
#
# `freertos_config` is the cross-platform STATIC at the repo root
# (component/freertos-kernel_config), which now also derives FREERTOS_PORT (GCC_RISC_V for
# BOUFFALO_CHIP) + FREERTOS_HEAP and add_subdirectory's the kernel itself — so freertos_kernel
# / FreeRTOS already exist here. Bouffalo's RISC-V-specific config (atomic_rtos.h path + mtime
# / trap-handler defs + the kernel-header feed) is attached to freertos_config from the
# freertos_port COMPONENT; it resolves at generate time, so it still reaches the kernel's
# compile.
if(NOT TARGET freertos_kernel)
    message(FATAL_ERROR
        "freertos_kernel target missing — component/freertos-kernel_config must be added "
        "(repo-root CMakeLists) before component (bouffalo_sdk)")
endif()

# bflb_autoconf must come first: its INTERFACE forces -include autoconf.h
# into every TU that links it (transitively, via bflb_lhal below).
add_subdirectory(bflb_autoconf)
add_subdirectory(freertos_port)
add_subdirectory(bflb_sdk_version)
add_subdirectory(bflb_sysinit)
# std must be added before lhal — lhal links against ${BOUFFALO_CHIP}_std.
add_subdirectory(${BOUFFALO_CHIP}_std)
add_subdirectory(bflb_lhal)
# bflb_log was deleted — logging is the cross-platform rxi_log (component/rxi_log_config, added at
# the component/ root), with the SDK's LOG_*/BFLB_LOG_* routed through bflb_log_compat.h.
# tlsf_config moved to the shared top-level component (component/tlsf_config), added by the
# repo-root CMakeLists before this subtree; the `tlsf_config` target already exists.
add_subdirectory(bflb_block_pool)
add_subdirectory(bflb_timestamp)
add_subdirectory(bflb_partition)
add_subdirectory(bflb_mtd)
add_subdirectory(bflb_sys)
# bflb_async_event deleted: WiFi events now dispatch synchronously (wifi4 plat_bouffalo_sdk.c /
# wl80211 wifi_mgmr.c), aligning to AmebaD's direct wifi_indication dispatch (no queue).
# bflb_power_mgmt is a BL616+ thing (clock_manager + pm_manager + tickless
# stack built around the LP-fw framework). BL602 predates all of this —
# its bl_lp.h has no chip-specific impl, several SDK symbols
# (iot2lp_para, HBN_*) don't exist on BL602, and bouffalo's BL602 power-
# mgmt path goes through wifi_hosal's own bflb_pm.c. Skip the lib on
# BL602 to avoid pulling in code that won't compile.
if(NOT "${BOUFFALO_CHIP}" STREQUAL "bl602")
    add_subdirectory(bflb_power_mgmt)
endif()
# FreeRTOS-Plus-TCP NetworkInterface for the wl80211 driver (BL616 / BL616CL) —
# replaces the removed lwIP netif glue. Self-gates to the wl80211 chips (early
# return() on bl602); re-supplies wl80211_tcpip_input / wl80211_output_raw.
add_subdirectory(freertos_tcp_port)

# Bouffalo's littlefs XIP-flash backend (lfs_xip_init + the four lfs_config
# read/prog/erase/sync callbacks). Always built so consumers of the
# `littlefs` target from component/thirdparty/ find a backend; tiny
# when nothing links it.
add_subdirectory(lfs_port)   # lfs_port (mirrors component/amebad/lfs_port)

# mbedtls port surface — mbedtls_port carries the runtime glue + HACC/PKA-backed alt impls AND
# (via its mbedtls_cfg INTERFACE) the whole Bouffalo mbedTLS config: MBEDTLS_USER_CONFIG_FILE
# + the MBEDTLS_*_ALT selections. Added before the upstream mbedtls pull below; its config
# INTERFACE is attached onto the upstream targets in the foreach further down.
add_subdirectory(mbedtls_port)

# -------------------------------------------------------------------------
# Upstream Mbed TLS v3.6.4 (component/mbedtls_config/mbedtls, clean upstream).
# Pulled here — not in component/thirdparty/CMakeLists.txt — because the
# upstream sources are unbuildable without the mbedtls_port config/headers,
# and nothing outside the Bouffalo SDK consumes upstream mbedtls. Keeps the
# dependency one-directional.
#
# Why v3.6.4 and not v3.6.5+: the 3.6.5 release reorganized the source
# tree (most primitives moved under tf-psa-crypto/drivers/builtin/src/)
# AND removed the classic MBEDTLS_*_ALT mechanism for the in-tree builtin
# drivers. Bouffalo's HW-accelerated alt impls (aes_alt.c, sha*_alt.c,
# bignum_alt.c, ecp_alt.c, …) rely on MBEDTLS_*_ALT to substitute the
# software implementations at compile time. Pinning at 3.6.4 keeps that
# contract intact while still being a supported LTS release.
#
# Targets produced (all STATIC — BUILD_SHARED_LIBS forced OFF):
#   mbedcrypto    crypto primitives + PSA + DRBG
#   mbedx509      X.509 cert handling
#   mbedtls       TLS protocol (depends on the above)
#   everest       Curve25519 fast impl (3rdparty submodule)
#   p256m         secp256r1 fast impl (3rdparty submodule)

# The upstream option knobs (ENABLE_TESTING OFF, USE_STATIC_MBEDTLS_LIBRARY, …) + the
# add_subdirectory of the submodule live in component/mbedtls_config. Bouffalo uses the
# upstream DEFAULT config + MBEDTLS_USER_CONFIG_FILE (mbedtls_user_config.h) — both carried by
# mbedtls_port's mbedtls_cfg INTERFACE, attached onto the upstream targets in the foreach
# below (no -DMBEDTLS_CONFIG_FILE here anymore).
# The port's alt headers (aes_alt.h etc.) need to be on the include path of every TU because
# upstream's public headers #include "aes_alt.h" unconditionally when MBEDTLS_AES_ALT is set.
include_directories("${CMAKE_CURRENT_LIST_DIR}/mbedtls_port")

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../mbedtls_config"
                 ${CMAKE_BINARY_DIR}/component/mbedtls_config
                 EXCLUDE_FROM_ALL)

# Wire the Bouffalo include paths (config + port) onto the upstream
# targets so the config file resolves and the alt headers are visible
# when MBEDTLS_*_ALT macros are on.
#
# Attach the Bouffalo mbedTLS config onto the upstream targets:
#   - mbedtls_cfg (INTERFACE from mbedtls_port): MBEDTLS_USER_CONFIG_FILE + the MBEDTLS_*_ALT
#     selections + the include dir for mbedtls_user_config.h / the *_alt.h headers. It's a pure
#     INTERFACE so it can attach onto upstream without the genex cycle that blocks linking
#     mbedtls_port itself onto mbedcrypto (port reads mbedcrypto's INTERFACE_INCLUDE_DIRECTORIES).
#   - the alt struct headers also need port's SOURCE_DIR on every TU (upstream's <mbedtls/aes.h>
#     #includes "aes_alt.h" when MBEDTLS_AES_ALT is set); fed via SOURCE_DIR (a fixed string,
#     one-way — feeding port's INTERFACE includes back would close the genex cycle).
#   - bflb_lhal: the alt headers #include the SEC driver headers (bflb_sec_aes.h → bflb_core.h →
#     the chip std register headers); link propagation carries bflb_lhal's whole transitive include
#     interface (incl. bl<chip>_std) onto every upstream mbedtls TU. No cycle (bflb_lhal doesn't
#     reference mbedcrypto). everest/p256m need it too (they pull psa/crypto → sha1.h). bflb_lhal
#     is add_subdirectory'd further down — a forward link ref is fine.
foreach(_t mbedcrypto mbedtls mbedx509 everest p256m)
    if(TARGET ${_t})
        target_link_libraries(${_t} PUBLIC mbedtls_cfg bflb_lhal)
        target_include_directories(${_t} PUBLIC
            "$<TARGET_PROPERTY:mbedtls_port,SOURCE_DIR>"
        )
        # MBEDTLS_FS_IO stays on (shared user config) -> x509_crt.c #include <dirent.h>, which the
        # t-head newlib ships as a `#error` stub. component/syscall_config supplies a real
        # <dirent.h>; put its dir FIRST so it shadows the stub. (parse_path is unreferenced here, so
        # it just needs to compile -> dead-strips at link; no opendir/readdir link dep.)
        target_include_directories(${_t} BEFORE PRIVATE
            "${AT_ROOT}/component/syscall_config")
        target_compile_options(${_t} PRIVATE -w)
    endif()
endforeach()

# Swap the bignum module for the Bouffalo PKA fork. Unlike aes/sha/ecp, upstream
# library/bignum.c has no whole-module MBEDTLS_*_ALT guard, so we can't select the HW
# modexp with a macro. Instead: exclude upstream's bignum.c from mbedcrypto (HEADER_FILE_ONLY
# keeps it in the target's file list but tells the generator not to compile it), and let
# mbedtls_port's forked bignum.c + bignum_alt.c (HW mbedtls_mpi_exp_mod) provide the
# mbedtls_mpi_* API instead. TARGET_DIRECTORY sets the property in mbedcrypto's dir scope
# (the source is added there). bignum_core.c / bignum_mod*.c stay upstream.
set_source_files_properties(
    "${CMAKE_CURRENT_LIST_DIR}/../mbedtls_config/mbedtls/library/bignum.c"
    TARGET_DIRECTORY mbedcrypto
    PROPERTIES HEADER_FILE_ONLY ON)
# Utility libs ported one-by-one from
# platform/bouffalo_sdk/components/utils/. All are header-only INTERFACEs
# or small STATIC libs; pull them all in unconditionally — they're tiny
# and consumers opt-in via target_link_libraries.
# bflb_libc (the Bouffalo-specific newlib retarget + printf + NuttX stdlib/string)
# was deleted — Bouffalo now uses the SAME libc as AmebaD: newlib + the shared
# component/syscall_config. Only two residues remain on the Bouffalo side:
#   syscall_port        the platform console backend (bflb_uart) for syscall_config
#   bflb_nuttx_compat   the 3 NuttX SDK compat headers the WiFi adapter still needs
add_subdirectory(syscall_port)
add_subdirectory(bflb_nuttx_compat)
add_subdirectory(monitor_port)        # Bouffalo port for the shared console engine (component/monitor_config)
add_subdirectory(rtc_time_port)       # Bouffalo port for the shared RTC seam (component/rtc_time_config): bflb_rtc + KV
add_subdirectory(board_flash_port)    # Bouffalo board_flash_protect for the shared flash write-protect (component/flash_cli_config)
add_subdirectory(ota_flash_port)      # BL602 OTA slot writer (component/ota_flash_config) over bflb_flash + pt_table + xz
add_subdirectory(mbedtls_bench_port)  # RISC-V mcycle timer override for the shared mbedtls_bench (component/mbedtls_bench_config)
add_subdirectory(bflb_getopt)
add_subdirectory(bflb_list)
add_subdirectory(bflb_queue)
add_subdirectory(bflb_tree)
add_subdirectory(bflb_ring_buffer)
add_subdirectory(bflb_math)
add_subdirectory(bflb_coredump)
add_subdirectory(${BOUFFALO_CHIP}_board_config)

# ELF -> flashable BIN pipeline (the bflb_add_executable() helper).
# Lives next to the rest of the SDK so consumers automatically get the
# helper after linking bouffalo_sdk — no separate include() needed.
add_subdirectory(bflb_image_tool)

# phyrf only matters when RF/Wi-Fi is enabled. rfparam (BL602-only in this
# fork — BL616 / BL616CL use a different RF parameter pipeline shipped inside
# their respective phyrf .a archives) needs phyrf's headers, so it gates on
# the same flag.
if(CONFIG_RF)
    add_subdirectory(${BOUFFALO_CHIP}_phyrf)
    if("${BOUFFALO_CHIP}" STREQUAL "bl602")
        add_subdirectory(bl602_rfparam)
    endif()
endif()

# wifi4 — Wi-Fi 4 stack. BL602-only (BL616 / BL616CL use wifi6 via the
# wl80211 + macsw + bl_wpa_supplicant trio below). The umbrella here just
# pulls the subtree in; the wifi4 dir's own CMakeLists adds the six
# sublibs and defines the `wifi4` INTERFACE roll-up.
if(CONFIG_WIFI4 AND "${BOUFFALO_CHIP}" STREQUAL "bl602")
    add_subdirectory(wifi4)
endif()

# Wi-Fi 6 stack on BL616 / BL616CL: three coupled components.
#   wl80211 — Bouffalo's 802.11 driver + wifi_mgmr API surface.
#   macsw   — MAC software layer (prebuilt .a per chip × variant +
#             macsw_plat.c shim).
#   bl_wpa_supplicant — Bouffalo port of wpa_supplicant.
# CONFIG_WIFI6 is the umbrella switch in proj.conf (matches upstream).
# CONFIG_WL80211 + CONFIG_BL_WPA_SUPPLICANT are individually gated to
# mirror upstream's Kconfig but in practice on this fork the three move
# together; we wire them inside the same CONFIG_WIFI6 block.
if(CONFIG_WIFI6 AND ("${BOUFFALO_CHIP}" STREQUAL "bl616" OR
                    "${BOUFFALO_CHIP}" STREQUAL "bl616cl"))
    add_subdirectory(macsw)
    add_subdirectory(bl_wpa_supplicant)
    if(CONFIG_WL80211)
        add_subdirectory(wl80211)
    endif()
endif()

# Generic RF parameter loader for non-BL602 chips (BL602 uses its own
# bl602_rfparam component, wired above). Gated on CONFIG_RF + chip.
if(CONFIG_RF AND NOT ("${BOUFFALO_CHIP}" STREQUAL "bl602"))
    add_subdirectory(rfparam)
endif()

# BLE/BT controller — Bouffalo's chip-specific prebuilt controller .a (the
# Zephyr-derived host + HCI transport were stripped; only the controller +
# its folded-in CONFIG_BT_* defs remain). Gated on CONFIG_BLUETOOTH; BL602 uses
# blecontroller (m1s1 profile), BL616 / BL616CL use btblecontroller.
if(CONFIG_BLUETOOTH)
    if("${BOUFFALO_CHIP}" STREQUAL "bl602")
        add_subdirectory(blecontroller)
    elseif("${BOUFFALO_CHIP}" STREQUAL "bl616" OR
           "${BOUFFALO_CHIP}" STREQUAL "bl616cl")
        add_subdirectory(btblecontroller)
    else()
        message(FATAL_ERROR
            "CONFIG_BLUETOOTH: unsupported BOUFFALO_CHIP '${BOUFFALO_CHIP}' "
            "(blecontroller / btblecontroller .a pruned to bl602/bl616/bl616cl).")
    endif()

    # Apache NimBLE host on top of the on-chip controller (structured HCI).
    if(CONFIG_NIMBLE_HOST)
        add_subdirectory(nimble_port)
    endif()
endif()

add_library(bouffalo_sdk INTERFACE)
target_link_libraries(bouffalo_sdk INTERFACE
    bflb_autoconf
    bflb_sdk_version
    freertos_port
    ${BOUFFALO_CHIP}_std
    bflb_lhal
    # newlib retarget core (usage reqs: <dirent.h> + transitive at_console) + the
    # Bouffalo console backend + the NuttX compat headers. The OBJECT libs' objects
    # are force-included separately via $<TARGET_OBJECTS:> below (they don't flow
    # through this INTERFACE intermediary).
    syscall_config
    bouffalo_syscall_port
    bflb_nuttx_compat
    tlsf_config
    rxi_log             # the single cross-platform logger (bflb_log deleted; SDK LOG_* via bflb_log_compat.h)
    bflb_block_pool
    bflb_timestamp
    bflb_partition
    bflb_mtd
    bflb_sys
    ${BOUFFALO_CHIP}_board_config
    # bflb_sysinit comes last so its archive is scanned AFTER bl<chip>_std,
    # which is the only thing that references sysinit_run_all.
    bflb_sysinit
)

# Optional: BL616+ power management (excluded on BL602 — see above).
if(TARGET bflb_power_mgmt)
    target_link_libraries(bouffalo_sdk INTERFACE bflb_power_mgmt)
endif()

# OBJECT libraries don't propagate their object files transitively through an
# INTERFACE intermediary — CMake only auto-extracts $<TARGET_OBJECTS:...> when
# the OBJECT lib is linked directly to a real target. Stitching them in
# explicitly here so symbols kept alive only by linker-script KEEP() rules
# (.fw_header, .version.sdk) actually reach the final ELF.
target_sources(bouffalo_sdk INTERFACE
    $<TARGET_OBJECTS:bflb_sdk_version>
    $<TARGET_OBJECTS:${BOUFFALO_CHIP}_board_config>
    # newlib retarget core + Bouffalo console backend. Force-included as objects
    # (not archive members) so their STRONG defs win: syscall_config's _open/_read/
    # _write/_lseek/malloc/_sbrk/environ beat libc's ENOSYS/brk stubs, and
    # bouffalo_syscall_port's at_console_default_backend beats the dispatcher's weak
    # default. An archive member wouldn't be pulled to override (no undefined ref).
    $<TARGET_OBJECTS:syscall_config>
    $<TARGET_OBJECTS:bouffalo_syscall_port>
)
if(CONFIG_RF)
    target_link_libraries(bouffalo_sdk INTERFACE ${BOUFFALO_CHIP}_phyrf)
    if("${BOUFFALO_CHIP}" STREQUAL "bl602")
        target_link_libraries(bouffalo_sdk INTERFACE bl602_rfparam)
    endif()
endif()

if(CONFIG_WIFI4 AND "${BOUFFALO_CHIP}" STREQUAL "bl602")
    target_link_libraries(bouffalo_sdk INTERFACE wifi4)
endif()

if(CONFIG_WIFI6 AND ("${BOUFFALO_CHIP}" STREQUAL "bl616" OR
                    "${BOUFFALO_CHIP}" STREQUAL "bl616cl"))
    target_link_libraries(bouffalo_sdk INTERFACE
        macsw
        bl_wpa_supplicant
    )
    if(TARGET wl80211)
        target_link_libraries(bouffalo_sdk INTERFACE wl80211)
    endif()
endif()

if(TARGET rfparam)
    target_link_libraries(bouffalo_sdk INTERFACE rfparam)
endif()

# Port adapter libs that always get built (lfs_port carries headers
# downstream consumers transitively need even when nothing
# references the symbols). Plus the chip's prebuilt BLE/BT controller when
# CONFIG_BLUETOOTH is on. Stitching them in the umbrella here so that downstream projects don't
# have to know which sub-targets exist — they just do
# `target_link_libraries(<exe> PRIVATE bouffalo_sdk)`.
# (mbedtls_port is linked further down, AFTER the upstream mbedtls archives — see there.)
target_link_libraries(bouffalo_sdk INTERFACE
    lfs_port
)
# The cross-platform interactive console: the shared engine + command modules + the Bouffalo
# port. All inside bouffalo_sdk's --start-group (they cross-ref: engine<->port seam, cmds pull
# the VFS/littlefs/mbedcrypto). main() calls monitor_start() + the *_cli_register() functions.
target_link_libraries(bouffalo_sdk INTERFACE
    monitor
    monitor_cmds
    monitor_port
    freertos_cli
)
# Shared startup helpers (component/app_startup_config): app_env_time_init / app_console_start /
# app_on_network_up, called from main() + the GOT_IP handler. Inside the --start-group (below), so its
# references into monitor / *_cli_register + the platform seams (strong in src/main.cpp) re-scan cleanly.
target_link_libraries(bouffalo_sdk INTERFACE app_startup)
# coreSNTP UTC time sync (component/sntp_time_config) — pulls coreSNTP + backoffAlgorithm
# transitively; main() calls sntp_sync_start(). Gives bl602 real UTC (date/sntp_update/timestamps).
target_link_libraries(bouffalo_sdk INTERFACE sntp_time)
# Retained wall clock over the bflb_rtc HBN counter + KV (component/bouffalo_sdk/rtc_time_port): the
# strong rtc_time_store / rtc_time_seed that sntp_time.c + main() call, mirroring AmebaD's rtc_time.c.
target_link_libraries(bouffalo_sdk INTERFACE rtc_time_port)
# Flash write-protect: the shared `flash` command + helpers (flash_cli) + the bl602 board_flash_protect
# primitive (board_flash_port). main() calls board_flash_lock_fw() + board_flash_log_status() at boot;
# app_console_register_platform() calls flash_cli_register().
target_link_libraries(bouffalo_sdk INTERFACE flash_cli board_flash_port)
# BL602 OTA slot writer (bflb_flash + pt_table + inline xz) for the shared `ota_http` command.
target_link_libraries(bouffalo_sdk INTERFACE ota_flash_port)

# zip/unzip console commands (component/monitor_config zip_cmd -> minizip). bl602's newlib malloc
# is syscall_config's malloc -> pvPortMalloc -> TLSF (big heap), so no --wrap needed (unlike AmebaD's
# tiny _sbrk arena). main() calls zip_cli_register().
target_link_libraries(bouffalo_sdk INTERFACE zip_cmd)

# iperf throughput probes (component/iperf_config) — main() registers the `iperf` console command.
# NOT on bl602: iperf's static ucTxBuf(8K)+ucRecvBuf(5.7K)+ucRxBuf(2K) = ~16 KB of .bss, which the
# heap-tight bl602 needs for the AWS mutual-TLS handshake peak (freeing them drops __HeapBase and
# gives the TLSF OCRAM pool a ~16 KB bigger *contiguous* run — the handshake was failing on
# fragmentation, not total size). bl616/cl keep iperf. main() gates iperf_cli_register() the same way.
if(NOT "${BOUFFALO_CHIP}" STREQUAL "bl602")
    target_link_libraries(bouffalo_sdk INTERFACE iperf)
endif()

# Shared WiFi provisioning + reconnect policy (component/wifi_service_config). main() calls
# wifi_service_start(); the bl602 wifi_port_* HAL impl + event routing live in src/main.cpp.
target_link_libraries(bouffalo_sdk INTERFACE wifi_service)

# Shared network-diagnostic console commands (component/net_cli_config): `resolve` + `ip6`. main()'s
# app_console_register_platform() calls net_cli_register() on all Bouffalo chips.
target_link_libraries(bouffalo_sdk INTERFACE net_cli)

# Minimal AWS IoT telemetry (component/aws_iot_config): direct coreMQTT over the mbedTLS TLS
# transport — same libraries + credential model as AmebaD, telemetry slice only (no coreMQTT-Agent /
# OTA, which don't fit bl602's heap). Pulls coremqtt + network_transport_mbedtls + mbedtls (resolved
# in the --start-group with the mbedtls archives below). bl602 only: aws_get_mac() + the aws_cli
# registration in src/main.cpp are under `#if defined BL602`; bl616/cl would leave aws_get_mac
# undefined. main() calls aws_cli_register(); the `aws` command provisions + starts telemetry.
if("${BOUFFALO_CHIP}" STREQUAL "bl602")
    target_link_libraries(bouffalo_sdk INTERFACE aws_iot)
    # AWS credential provisioning pastes a ~1.6 KB base64 body as ONE `aws cert/key/ca <b64>` CLI
    # argument. The shared monitor's line buffer defaults to 512 on RISC-V (monitor.c) — too small,
    # so the paste would be silently truncated. Bump it to 2560 (same as AmebaD KM4). Costs ~2 KB
    # .bss on the tight bl602 heap, but there's no other way to get the DER creds onto the device.
    target_compile_definitions(monitor PRIVATE MONITOR_CLI_MAX_INPUT=2560)
endif()

# c-ares async DNS resolver (component/c-ares_config) — the SAME cross-platform resolver AmebaD uses
# under CARES=ON (mirrors project/amebad/app_km4/CMakeLists.txt:631-643). make ... CARES=ON
# (-> -DAT_ENABLE_CARES=ON) links cares_port and stamps AT_PROJECT_CARES onto the DNS call sites:
#   network_transport_sockets (tcp_sockets_wrapper.c: the AWS TLS/MQTT resolve) and
#   sntp_time (sntp_time.c: the SNTP resolve)
# so both route through cares_resolve() (connected-UDP shim + select loop over syscall_config's BSD
# shim) instead of FreeRTOS_getaddrinfo. app_on_network_up() (GOT_IP handler) then re-points the c-ares
# channel at the DHCP-learned DNS via app_network_dns_reinit() (strong override in c-ares_config).
# cares_port sits inside the umbrella's --start-group (below), so its cross-refs re-scan cleanly.
if(AT_ENABLE_CARES)
    target_link_libraries(bouffalo_sdk INTERFACE cares_port)
    target_compile_definitions(network_transport_sockets PRIVATE AT_PROJECT_CARES)
    target_link_libraries(network_transport_sockets PRIVATE cares_port)
    target_compile_definitions(sntp_time PRIVATE AT_PROJECT_CARES)
    target_link_libraries(sntp_time PRIVATE cares_port)
    # Shared `resolve` command uses c-ares when stamped (else the FreeRTOS+TCP resolver fallback).
    target_compile_definitions(net_cli PRIVATE AT_PROJECT_CARES)
    target_link_libraries(net_cli PRIVATE cares_port)
endif()

# QUIC client self-test (component/quic_selftest_config): ngtcp2 + picotls/mbedTLS TLS 1.3 -- the SAME
# cross-platform stack AmebaD uses under QUIC=ON. make ... QUIC=ON (-> -DAT_ENABLE_QUIC=ON) links
# quic_selftest + the ngtcp2/picotls archives into the umbrella --start-group (they have circular refs
# with mbedcrypto), and defines AT_PROJECT_QUIC so main()'s app_network_up_start_services() launches
# quic_test_start() on network-up. bl602 builds in QUIC_SMOKE mode (tiny handshake+download, 32 KB
# stack) -- best-effort, since the TLS 1.3 handshake peak (~44 KB) may exceed its ~52 KB contiguous
# heap. No TF-M is involved (Bouffalo has no TrustZone; HW crypto is mbedtls_port NS-direct).
if(AT_ENABLE_QUIC)
    target_link_libraries(bouffalo_sdk INTERFACE
        quic_selftest
        ngtcp2_crypto_picotls
        ngtcp2_static
        picotls_core
        picotls_mbedtls
    )
    target_compile_definitions(bouffalo_sdk INTERFACE AT_PROJECT_QUIC)
    if("${BOUFFALO_CHIP}" STREQUAL "bl602")
        target_compile_definitions(quic_selftest PRIVATE QUIC_SMOKE)
    endif()
endif()

# The Mbed TLS crypto self-tests (bench + crt_bundle + pkcs11) are OPT-IN, mirroring AmebaD's
# BENCH/CRT_BUNDLE/PKCS11=ON images: linking all three pulls RSA/full-ECP/ChaChaPoly + the ~56 KB
# CA bundle + corePKCS11/PSA, which overflows bl602's xip_memory region by ~100 KB. Default OFF so
# the base image (monitor/sntp/zip/iperf) fits; `make ... CRYPTO_SELFTESTS=ON` (-> -DAT_CRYPTO_SELFTESTS=ON)
# builds a diagnostic image with the `bench`/`crt_bundle`/`pkcs11` console commands. main() gates the
# matching *_cli_register() calls on AT_CRYPTO_SELFTESTS, propagated to the app via the INTERFACE define.
option(AT_CRYPTO_SELFTESTS "Bouffalo: build the Mbed TLS crypto self-tests (bench/crt_bundle/pkcs11)" OFF)
if(AT_CRYPTO_SELFTESTS)
    # Mbed TLS crypto microbenchmark (component/mbedtls_bench_config) + its RISC-V mcycle timer
    # override (mbedtls_bench_port); the bench pulls extra mbedcrypto symbols (RSA/full-ECP/ChaChaPoly)
    # resolved in the --start-group with the mbedtls archives below. mbedtls_bench_port supplies
    # bench_timer_* (mcycle CSR + bflb clock).
    target_link_libraries(bouffalo_sdk INTERFACE mbedtls_bench mbedtls_bench_port)
    # Compressed Mozilla CA root bundle + the `crt_bundle` command (component/esp_crt_bundle_config).
    # crt_bundle_cli pulls esp_crt_bundle (mbedx509/mbedcrypto, in the --start-group below). No
    # network_transport on bl602 -> the `connect` subcommand is compiled out; info/verify/addca remain.
    target_link_libraries(bouffalo_sdk INTERFACE crt_bundle_cli)
    # corePKCS11 token self-test + the `pkcs11` command (component/pkcs11_selftest_config): corepkcs11
    # + the fopen POSIX PAL over LittleFS + mbedcrypto PSA (resolved in the --start-group below).
    target_link_libraries(bouffalo_sdk INTERFACE pkcs11_selftest)
    target_compile_definitions(bouffalo_sdk INTERFACE AT_CRYPTO_SELFTESTS)
endif()

# FreeRTOS-Plus-TCP: the wl80211 NetworkInterface bridge + the core stack + its
# BufferAllocation/compiler port. Only defined on the wl80211 chips (bl616/cl);
# all three sit in the --start-group below so the stack<->bridge<->driver symbol
# cycle (wl80211_tcpip_input etc.) resolves at the final image link.
if(TARGET bouffalo_freertos_tcp)
    target_link_libraries(bouffalo_sdk INTERFACE
        bouffalo_freertos_tcp
        freertos_plus_tcp
        freertos_plus_tcp_port
    )
endif()
if(TARGET blecontroller)
    target_link_libraries(bouffalo_sdk INTERFACE blecontroller)
endif()
if(TARGET btblecontroller)
    target_link_libraries(bouffalo_sdk INTERFACE btblecontroller)
endif()
# NimBLE host (pulls its mbedcrypto/mbedtls_config deps + the controller transitively).
if(TARGET nimble)
    target_link_libraries(bouffalo_sdk INTERFACE nimble)
endif()

# Cross-platform third-party libs from component/thirdparty/ (built by its
# own CMakeLists, which knows nothing about Bouffalo) + the upstream mbedtls
# targets pulled in by the upstream-mbedtls block above. Sticking them on the
# umbrella here keeps the dependency one-directional: third-party doesn't peek
# at bouffalo_sdk.
#
# mbedcrypto / mbedtls / mbedx509: the upstream archives. mbedtls_port (which
# now also carries the HACC/PKA MBEDTLS_*_ALT impls, formerly bflb_mbedtls_hw_acc)
# is linked AFTER them ON PURPOSE: those alt impls define the same
# mbedtls_sha*/aes/ecp/bignum symbols as upstream, and the MBEDTLS_*_ALT macros
# don't currently reach the upstream compile (upstream still emits the software
# impls), so within the link group the alt objects must come last — upstream
# resolves the symbols first and the alt members are simply never pulled (they
# act as fallbacks). Linking mbedtls_port before mbedcrypto pulls both and the
# linker reports multiple-definition. The port's platform hooks (entropy /
# timing) still resolve fine via the --start/--end-group re-scan.
target_link_libraries(bouffalo_sdk INTERFACE
    zlibstatic
    cjson
    cjson_utils
    littlefs
    mbedtls
    mbedcrypto
    mbedx509
    mbedtls_port
)

# TLS_BACKEND=picotls (bl602 AWS): picotls_core + picotls_mbedtls ride mbedcrypto/PSA + have circular
# refs with it, so they belong inside the --start-group above (same as AmebaD's _aws_grp).
if("${BOUFFALO_CHIP}" STREQUAL "bl602" AND BOUFFALO_TLS_BACKEND STREQUAL "picotls")
    target_link_libraries(bouffalo_sdk INTERFACE picotls_core picotls_mbedtls)
endif()

# Bouffalo SDK keeps its -march/-mabi/include flags on the sdk_intf_lib
# INTERFACE target. Without linking against it zlib gets the toolchain's
# default ABI (RV64GC) and bl616 link fails with "ABI is incompatible
# with that of the selected emulation". Wired here, not on zlibstatic's
# own block in thirdparty/, because sdk_intf_lib is a Bouffalo-only
# target — keeping the dependency one-directional.
if(TARGET sdk_intf_lib)
    target_link_libraries(zlibstatic PRIVATE sdk_intf_lib)
endif()

# Open a linker group around the umbrella's transitive archives. Closing --end-group
# is deferred to project scope by component/CMakeLists.txt's
# _bouffalo_sdk_close_link_group() — the only way to guarantee the `-Wl,--end-group`
# token lands past every external lib (cjson / mbedtls / …) the umbrella links, since
# `target_link_options` emits before `target_link_libraries` in the final link line.
#
# Reasons for each piece:
#   --start-group: bouffalo_sdk has cyclic refs across archives
#       (bl<chip>_std <-> bflb_sysinit, wifi_manager <-> wpa_supplicant,
#        libfirmware.a <-> wifi_manager, …) that single-pass ld can't resolve.
#   --wrap=setenv/unsetenv: env.c (syscall_config) intercepts setenv/unsetenv to
#       persist to /KV littlefs files (__real_setenv / __wrap_setenv) — same as
#       AmebaD app_km4. (The old -u,_malloc_r/_sbrk_r/__wrap_sprintf force-pulls are
#       gone: syscall_config's malloc/_sbrk are force-included as OBJECTs above, and
#       printf/sprintf now come from newlib — bflb_libc's wrapped printf was deleted.)
#   -Wno-maybe-uninitialized: the toolchain enables -flto on link, so gcc replays
#       compilation of every TU inside the prebuilt vendor archives (libwl80211_*.a /
#       libphyrf_*.a — shipped LTO-fat). That replay surfaces upstream warnings
#       (cntrl.c / ap.c / scan.c / phy.h) we can't touch. Compile-time
#       -Wmaybe-uninitialized stays on for the project's own sources.
target_link_options(bouffalo_sdk INTERFACE
    -Wl,--start-group
    -Wl,--wrap=setenv
    -Wl,--wrap=unsetenv
    -Wno-maybe-uninitialized
)

# Close the --start-group above. `-Wl,--end-group` is smuggled in as a library entry
# (not a link option) so CMake appends it to INTERFACE_LINK_LIBRARIES — emitted AFTER
# the link-options block and after every archive the umbrella transitively pulls. The
# cmake_language(DEFER) to project scope guarantees it is the very LAST entry, past
# every external lib (cjson / mbedtls / …) the umbrella and its subtrees pull in.
# Lives here with --start-group (was in component/CMakeLists.txt).
function(_bouffalo_sdk_close_link_group)
    target_link_libraries(bouffalo_sdk INTERFACE -Wl,--end-group)
endfunction()
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR}
    CALL _bouffalo_sdk_close_link_group)
