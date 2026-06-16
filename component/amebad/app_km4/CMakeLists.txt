# AmebaD KM4 HP application image (image2, non-secure) — produces
# target_img2_km4.axf. **Bootstrap / WIP**: the full KM4 image2 links the
# entire WiFi / network / mbedTLS / BT / AT stack (dozens of prebuilt .a plus
# source groups). This wires the driver-level pieces (FreeRTOS HP, ram_hp,
# board config, monitor, PMC) and the image2 ld; the rest is future work.
#
# Mirrors project_hp/asdk/Makefile linker_image2_ns:
#   RAM_OBJS -> FreeRTOS(HP) + ram_hp + usrcfg_* + monitor_km4 + main.c
#   ROM      -> rlx8721d_rom_symbol_acut.ld (HP non-secure PROVIDE table;
#               CONFIG_LINK_ROM_SYMB=1, no rom_ns.a). Common HAL (UART/RTC/...)
#               comes from ROM here, NOT ram_common — like bootloader_km4.
#   APP_LIB  -> prebuilt lib_pmc_hp.a (+ lib_wlan.a / lib_wps.a for WiFi) + -lm
#               -lstdc++ (KM4 image has C++).
#
# img2 ld: rlx8721d_img2_is.ld (non-TrustZone) -> INCLUDE rlx8721d_layout_is.ld
#   (BD_RAM_NS / KM4_IMG2 @ 0x0e000000 / PSRAM_NS). RSIP (when enabled) @
#   the XIP cache base 0x0e000000.
#
# main.c + the linker scripts are project-local copies of the SDK
# (src_hp/main.c, project_hp ld/), flat in this folder.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

# Project config headers (platform_opts / Kconfig autoconf / main.h + the base
# FreeRTOSConfig path) this app owns. Exposed as an INTERFACE so amebad_sdk_includes
# links it (per-core: only the matching app defines amebad_proj_config) instead of a
# raw target_include_directories.
add_library(amebad_proj_config INTERFACE)
target_include_directories(amebad_proj_config SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

foreach(_p
        "${CMAKE_CURRENT_SOURCE_DIR}/main.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/rlx8721d_img2_is.ld"
        "${CMAKE_CURRENT_SOURCE_DIR}/rlx8721d_layout_is.ld"
        "${CMAKE_CURRENT_SOURCE_DIR}/rlx8721d_rom_symbol_acut.ld")
    if(NOT EXISTS "${_p}")
        message(FATAL_ERROR "amebad/app_km4: missing '${_p}'")
    endif()
endforeach()

# amebad_pmc_hp (KM4 PMC prebuilt lib) is defined by the pmu/ component;
# amebad_wlan / amebad_wps (host WiFi driver libs) by the wlan/ component.
# (amebad_wlan / amebad_wps intentionally not linked — this minimal image2 has
# no WiFi. Add them to the link group when the WiFi stack is wired.)

# Linker script: cat img2_is.ld + rom_symbol_acut.ld; img2_is.ld does
# INCLUDE "rlx8721d_layout_is.ld", resolved through -L below.
set(_img2_ld     "${CMAKE_CURRENT_SOURCE_DIR}/rlx8721d_img2_is.ld")
set(_rom_sym_ld  "${CMAKE_CURRENT_SOURCE_DIR}/rlx8721d_rom_symbol_acut.ld")
set(_rom_fwlib_ld "${AMEBAD_FWLIB_ROM_KM4_LD}")                      # KM4 fwlib HAL ROM symbols (fwlib)
set(_rom_str_ld  "${AMEBAD_STRING_ROM_KM4_LD}")                      # KM4 swlib-string ROM symbols (string_rom)
set(_rom_cmsis_ld "${AMEBAD_CMSIS_ROM_KM4_LD}")                      # KM4 CMSIS mpu/DSP ROM symbols (cmsis_rom)
set(_rom_wifi_ld "${CMAKE_CURRENT_SOURCE_DIR}/rom_symbol_wifi.ld")   # extra ROM PROVIDEs (PAD_CMD) for lib_wlan
set(_rom_mbed_ld "${CMAKE_SOURCE_DIR}/component/amebad/mbedtls_rom/mbedtls_rom.ld")  # ROM mbedTLS 2.4.0 + ssl ram map symbols
set(_layout_ld   "${CMAKE_CURRENT_SOURCE_DIR}/rlx8721d_layout_is.ld")
set(_combined_ld "${CMAKE_CURRENT_BINARY_DIR}/rlx8721d.ld")
add_custom_command(
    OUTPUT  "${_combined_ld}"
    COMMAND "${CMAKE_COMMAND}" -E cat "${_img2_ld}" "${_rom_sym_ld}" "${_rom_fwlib_ld}" "${_rom_str_ld}" "${_rom_cmsis_ld}" "${_rom_wifi_ld}" "${_rom_mbed_ld}" > "${_combined_ld}"
    DEPENDS "${_img2_ld}" "${_rom_sym_ld}" "${_rom_fwlib_ld}" "${_rom_str_ld}" "${_rom_cmsis_ld}" "${_rom_wifi_ld}" "${_rom_mbed_ld}" "${_layout_ld}"
    COMMENT "concat KM4 image2 linker script"
    VERBATIM
)
add_custom_target(amebad_km4_img2_ld DEPENDS "${_combined_ld}")

add_executable(target_img2_km4
    "${CMAKE_CURRENT_SOURCE_DIR}/main.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/cm_backtrace_stub.c"   # weak no-op (CmBacktrace lib not in this checkout)
    "${CMAKE_CURRENT_SOURCE_DIR}/minimal_stubs.c"       # weak CRYPTO_Init / rtw_if_wifi_thread (no TLS/WiFi)
    "${CMAKE_CURRENT_SOURCE_DIR}/wifi_scan_compat.c"    # weak wifi_mode + connect/EAP/promisc stubs
    "${CMAKE_CURRENT_SOURCE_DIR}/freertos_tcp_compat.c" # weak lwip stubs (lwip IP not linked; FreeRTOS+TCP instead)
)
# (lfs_selftest moved into the amebad_lfs_port archive so ALL LittleFS .text is
# pinned to the XIP tail by the ld script — keeps boot/WiFi code addresses stable.)

# Diagnostics (iperf throughput + ROM-mbedTLS / wolfCrypt benchmarks). There is
# no --gc-sections on this link, so unused TUs are only removed by excluding the
# source. The QUIC image drops them all (smaller KM4 image2 .text -> shifts the
# XIP boot path away from the layout-sensitive hang; see
# doc/amebad_xip_layout_boot_hang.md). main.c gates the matching calls under
# !AT_PROJECT_QUIC.
# corePKCS11 token self-test image also drops these diags: the ROM-mbedTLS KAT (rom_mbedtls*)
# would put a second, ABI-incompatible mbedTLS world next to the upstream one this image links,
# and the wolfCrypt bench is on the way out. Declared here so the gate below can see it.
option(AMEBAD_ENABLE_PKCS11 "Build the corePKCS11 token self-test (upstream mbedTLS backend + LittleFS PAL)" OFF)
# The crypto microbenchmark (make BENCH=ON) links the SAME upstream mbedTLS as QUIC/PKCS11, so
# it likewise drops the ROM-mbedTLS diag TUs (a second, ABI-incompatible mbedTLS world).
option(AMEBAD_ENABLE_CRYPTOBENCH "Build the upstream Mbed TLS v3.6.4 microbenchmark ([MCB] scores)" OFF)
# Minimal image (make MINIMAL=ON): WiFi connect (+ optional TF-M) only. Drops the diagnostic
# TUs below AND, via AT_PROJECT_MINIMAL on main.c / tfm_psa_selftest.c, the matching calls
# (iperf, LFS self-test, ROM-mbedTLS KAT, the TF-M HW-crypto KAT/GCM bench). The TF-M PSA
# ping + eFuse-HUK probe stay (they ARE the "TF-M is alive" proof).
option(AMEBAD_ENABLE_MINIMAL "Strip diagnostics: WiFi-connect-only image (pairs with TFM)" OFF)
if(AMEBAD_ENABLE_MINIMAL)
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/main.c"
                 APPEND PROPERTY COMPILE_DEFINITIONS AT_PROJECT_MINIMAL)
endif()
# AWS IoT Core MQTT/TLS demo (make AWS=ON): replaces the iperf diagnostics with an MQTT
# client (coreMQTT over the mbedTLS TLS transport). Like QUIC/PKCS11 it drops the iperf +
# ROM-mbedTLS KAT TUs (it links the upstream mbedTLS, ABI-incompatible with the ROM one).
# AWS=ON implies STORAGE=ON (the private key lives in TF-M Protected Storage). Declared here
# so the diag gate below can see it.
option(AMEBAD_ENABLE_AWS_IOT "Build the AWS IoT Core MQTT/TLS demo (coreMQTT + mbedTLS; key in TF-M PS)" OFF)
if(NOT AMEBAD_ENABLE_QUIC AND NOT AMEBAD_ENABLE_PKCS11 AND NOT AMEBAD_ENABLE_CRYPTOBENCH
   AND NOT AMEBAD_ENABLE_MINIMAL AND NOT AMEBAD_ENABLE_AWS_IOT)
    target_sources(target_img2_km4 PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/iperf_client.c"        # iperf2 TCP client (TX throughput to WSL iperf -s)
        "${CMAKE_CURRENT_SOURCE_DIR}/iperf_server.c"        # iperf2 TCP sink (RX throughput from WSL iperf -c)
        "${CMAKE_CURRENT_SOURCE_DIR}/iperf_udp_client.c"    # iperf2 UDP blast (bottleneck discriminator: driver vs TCP pacing)
        "${CMAKE_CURRENT_SOURCE_DIR}/iperf_udp_server.c"    # UDP RX sink (discriminator: is the RX/ACK delivery path tick-quantized?)
        "${CMAKE_CURRENT_SOURCE_DIR}/rom_mbedtls_kat.c"     # known-answer test of the ROM mbedTLS 2.4.0 software crypto
        "${CMAKE_SOURCE_DIR}/component/amebad/mbedtls_rom/rom_mbedtls.c"  # ROM mbedTLS ram-map glue (use_hw_crypto_func=0)
        "${CMAKE_CURRENT_SOURCE_DIR}/rom_hwcrypto_test.c"   # HW-vs-SW compare of rom_ssl_ram_map.h hw_crypto_* (IPSEC engine)
    )
endif()
# WIP: the image2 link still needs the app/WiFi/mbedTLS/lwIP/log/os_dep
# stack (~30 undefined symbols at bootstrap). EXCLUDE_FROM_ALL keeps the
# default KM4 build (bootloader_km4 + the driver libs) green; build this
# target explicitly to continue closing the link:
#   cmake --build <dir> -t target_img2_km4
set_target_properties(target_img2_km4 PROPERTIES EXCLUDE_FROM_ALL TRUE)
set_property(TARGET target_img2_km4 APPEND PROPERTY LINK_DEPENDS "${_combined_ld}")
add_dependencies(target_img2_km4 amebad_km4_img2_ld)

# BLE images on top of component/amebad/hci (no btgap.a):
#   AMEBAD_ENABLE_HCI    — raw HCI / DTM bridge test image (auto-enter bridge).
#   AMEBAD_ENABLE_NIMBLE — Apache NimBLE host (LE/GATT/Mesh/coex), via component/amebad/nimble_port.
# Both ride the same controller bring-up (amebad_hci + firmware blob; the newlib
# syscalls come from the syscalls core + amebad_syscall_port console backend, linked
# unconditionally below). NimBLE
# additionally links the `nimble` lib. Pick one. OFF by default.
option(AMEBAD_ENABLE_HCI    "Build the BLE controller HCI test image (bring-up + HCI bridge)" OFF)
option(AMEBAD_ENABLE_NIMBLE "Build the NimBLE host image (LE/GATT/Mesh/coex on amebad_hci)" OFF)

# The console (FreeRTOS-Plus-CLI over the LOGUART) is now the DEFAULT, built by
# component/amebad/monitor (amebad_monitor + amebad_monitor_common, in _diag_grp below) and
# started unconditionally by main.c (except the QUIC image). freertos_cli — the FreeRTOS-Plus-CLI
# core archive the monitor links — must be a real name in the cross_refs LINK_GROUP (the
# freertos-cli_config INTERFACE wouldn't expand there); it rides the _diag_grp so it is dropped
# alongside the monitor in the QUIC image.

set(_hci_grp "")
if(AMEBAD_ENABLE_HCI OR AMEBAD_ENABLE_NIMBLE)
    # Shared controller bits — bring-up via hci_tp_open() (component/amebad/hci/
    # hci_nostack.c) + the firmware blob in the EXECUTABLE so its STRONG defs
    # override the weak {0} stubs (a blob inside the archive wouldn't get pulled).
    # newlib syscalls now come from the syscalls core (linked below).
    set(_hci_grp ",amebad_hci")
    set(_hci_root "${CMAKE_SOURCE_DIR}/component/amebad/hci")
    if(AMEBAD_HCI_FW_MP)
        set(_hci_fw_src "${_hci_root}/hci_mp_patch.c")
    else()
        set(_hci_fw_src "${_hci_root}/hci_normal_patch.c")
    endif()
    set_source_files_properties("${_hci_fw_src}" PROPERTIES
        COMPILE_OPTIONS "-Wno-unused-variable;-Wno-unused-const-variable")
    target_sources(target_img2_km4 PRIVATE
        "${_hci_fw_src}")
endif()
if(AMEBAD_ENABLE_NIMBLE)
    # NimBLE SM crypto now rides upstream mbedTLS (mbedtls_sm_glue.c), not wolfCrypt:
    # The mbedtls_config umbrella + its archives (crypto/x509/tls + port HW-ALT/entropy) join the
    # group. All of mbedtls_config's in-group deps are listed so no member resolves group-external
    # (LINK_GROUP needs real archive names — the umbrella INTERFACE wouldn't expand inside it).
    set(_hci_grp ",amebad_hci,nimble,nimble_config,mbedcrypto,mbedx509,mbedtls,mbedtls_port,mbedtls_config")
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/main.c"
                 APPEND PROPERTY COMPILE_DEFINITIONS AT_PROJECT_NIMBLE)
elseif(AMEBAD_ENABLE_HCI)
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/main.c"
                 APPEND PROPERTY COMPILE_DEFINITIONS AT_PROJECT_HCI)
endif()

# QUIC self-test (ngtcp2 + picotls TLS 1.3 on the mbedTLS/PSA backend — the wolfSSL
# replacement). ngtcp2 + the picotls binding + picotls + mbedcrypto/x509 + the mbedTLS
# FreeRTOS-threading/entropy glue join the cross_refs group; they are mutually referenced.
# AT-command / monitor shell libs: linked by default, dropped from the QUIC
# image (shell init is gated off in main.c under AT_PROJECT_QUIC). Removing them
# changes the KM4 image2 XIP relative layout (boot-hang dodge) and shrinks it.
set(_diag_grp ",amebad_at_cmd,amebad_monitor,amebad_monitor_common,freertos_cli")

set(_quic_grp "")
if(AMEBAD_ENABLE_QUIC)
    set(_diag_grp "")
    # The hand-tuned flat --start-group needs real archive names (CMake won't expand the
    # ngtcp2_config / picotls_config INTERFACE aggregates inside a LINK_GROUP), so list the
    # members. ngtcp2_crypto_picotls (picotls binding) replaces ngtcp2_crypto (wolfSSL).
    set(_quic_grp ",ngtcp2_crypto_picotls,ngtcp2_static,picotls_core,picotls_mbedtls,mbedcrypto,mbedx509,mbedtls,mbedtls_port,mbedtls_config")
    target_sources(target_img2_km4 PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/quic_test.c")   # newlib syscalls now from the syscalls core
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/main.c"
                 APPEND PROPERTY COMPILE_DEFINITIONS AT_PROJECT_QUIC)
endif()

# corePKCS11 token self-test (AMEBAD_ENABLE_PKCS11): the software PKCS#11 token over the
# upstream mbedTLS backend (component/amebad/mbedtls_port) with object storage on LittleFS — the
# reused upstream POSIX PAL (fopen/remove) over the AmebaD newlib VFS. corePKCS11 + mbedcrypto
# /mbedx509 + the mbedTLS FreeRTOS-threading/entropy glue join the cross_refs group (real
# archive names; the _config/umbrella INTERFACEs don't expand inside LINK_GROUP).
set(_pk11_grp "")
if(AMEBAD_ENABLE_PKCS11)
    set(_pk11_grp ",corepkcs11,mbedcrypto,mbedx509,mbedtls,mbedtls_port,mbedtls_config")
    set(_pal "${CMAKE_SOURCE_DIR}/component/corepkcs11_config/corepkcs11/source/portable/os")
    target_sources(target_img2_km4 PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/pkcs11_selftest.c"
        "${_pal}/core_pkcs11_pal_utils.c"      # label <-> filename
        "${_pal}/posix/core_pkcs11_pal.c")     # fopen-based PAL over the AmebaD VFS/LittleFS
    target_include_directories(target_img2_km4 PRIVATE "${_pal}")
    set_source_files_properties(
        "${_pal}/core_pkcs11_pal_utils.c" "${_pal}/posix/core_pkcs11_pal.c"
        PROPERTIES COMPILE_OPTIONS "-w")       # silence vendor PAL warnings under -Werror
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/main.c"
                 APPEND PROPERTY COMPILE_DEFINITIONS AT_PROJECT_PKCS11)
endif()

# Crypto microbenchmark (AMEBAD_ENABLE_CRYPTOBENCH): the [MCB] scores for upstream Mbed TLS
# v3.6.4 software crypto (SHA/AES/GCM/ChaChaPoly + ECDSA/ECDHE/X25519/RSA), the apples-to-apples
# successor to the removed wolfCrypt [WCB] bench. mbedcrypto + the FreeRTOS-threading/entropy glue
# join the cross_refs group (real archive names; the _config umbrellas don't expand in LINK_GROUP).
set(_bench_grp "")
if(AMEBAD_ENABLE_CRYPTOBENCH)
    set(_bench_grp ",mbedcrypto,mbedx509,mbedtls,mbedtls_port,mbedtls_config")
    target_sources(target_img2_km4 PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/mbedtls_bench.c")
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/main.c"
                 APPEND PROPERTY COMPILE_DEFINITIONS AT_PROJECT_CRYPTOBENCH)
endif()

# AWS IoT Core MQTT/TLS demo (AMEBAD_ENABLE_AWS_IOT): coreMQTT v5.0.2 over the FreeRTOS
# reference mbedTLS TLS transport (component/network_transport) on FreeRTOS-Plus-TCP, with
# backoffAlgorithm reconnect. The endpoint + client cert are provisioned to LittleFS, the
# private key to TF-M Protected Storage (AWS=ON implies STORAGE=ON). core_mqtt +
# backoff_algorithm + the network_transport_mbedtls stack + mbedcrypto/x509/tls + the mbedTLS
# FreeRTOS-threading/entropy glue join the cross_refs group (real archive names; the _config
# umbrellas don't expand inside LINK_GROUP). efuse_api.c (not in amebad_mbed) is added for the
# BLE-MAC read (efuse_logical_read 0x190); the device name = BLE MAC = MQTT client id.
set(_aws_grp "")
if(AMEBAD_ENABLE_AWS_IOT)
    # Only the mutually-circular mbedTLS archives need the --start-group (cross_refs). core_mqtt
    # (leaf — coreMQTT reaches the network only through TransportInterface_t function pointers) +
    # backoff_algorithm + the network_transport_* stack are one-directional (they call mbedTLS /
    # FreeRTOS-Plus-TCP, nothing calls back), so they link as ordinary libs below; CMake topo-sorts
    # them ahead of the group. Putting them IN the group instead drags their coremqtt_config /
    # freertos-plus-tcp_config / freertos_config umbrellas back into it -> a CMake dependency cycle.
    set(_aws_grp ",mbedcrypto,mbedx509,mbedtls,mbedtls_port,mbedtls_config")
    target_link_libraries(target_img2_km4 PRIVATE
        core_mqtt backoff_algorithm
        network_transport_mbedtls network_transport_mbedtls_bio network_transport_sockets)
    target_sources(target_img2_km4 PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/aws_iot_demo.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/aws_provision.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/aws_cli.c"                    # `set` / `blemac` console commands
        "${CMAKE_SOURCE_DIR}/component/amebad/mbed/efuse_api.c")   # efuse_logical_read (BLE MAC @ 0x190); not built into amebad_mbed
    set_source_files_properties("${CMAKE_SOURCE_DIR}/component/amebad/mbed/efuse_api.c"
        PROPERTIES COMPILE_OPTIONS "-w")
    # FreeRTOS-Plus-CLI header for aws_cli.c (FreeRTOS_CLIRegisterCommand); the freertos_cli
    # archive is already in the link group via _diag_grp.
    target_include_directories(target_img2_km4 PRIVATE
        "${CMAKE_SOURCE_DIR}/component/freertos-cli_config/freertos-cli")
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/main.c"
                 APPEND PROPERTY COMPILE_DEFINITIONS AT_PROJECT_AWS_IOT)
    # AWS is a connect-and-run image: strip the boot diagnostics like MINIMAL — iperf / LFS
    # self-test / ROM-mbedTLS KAT and the TF-M HW-crypto KAT/GCM throughput bench (~40 s before
    # WiFi). Credentials live in TF-M PS (not LittleFS — see aws_provision.c), so skipping
    # lfs_selftest is REQUIRED here: it is the only caller of the LittleFS mount, whose XIP-pinned
    # .text hangs in this image's layout. Skipping it means the LFS code is never executed.
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/main.c"
                 APPEND PROPERTY COMPILE_DEFINITIONS AT_PROJECT_MINIMAL)
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tfm_psa_selftest.c"
                 APPEND PROPERTY COMPILE_DEFINITIONS AT_PROJECT_MINIMAL)
endif()

# POSIX-fd VFS self-test (vfs_selftest, gated by the option in component/amebad/CMakeLists.txt).
# main.c calls it; the impl is in the shared lfs_config's lfs_selftest.c (#ifdef AMEBAD_VFS_SELFTEST).
# app_km4 is the consumer that owns the option, so per littlefs_config's design it enables the impl
# there + attaches the AmebaD specifics vfs_selftest needs (FreeRTOS+TCP sockets, syscall_config's
# syscalls.h) — kept out of the shared lfs_config / the mid-tree lfs_port library.
if(AMEBAD_VFS_SELFTEST)
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/main.c"
                 APPEND PROPERTY COMPILE_DEFINITIONS AMEBAD_VFS_SELFTEST)
    target_compile_definitions(lfs_config PRIVATE AMEBAD_VFS_SELFTEST)
    target_link_libraries(lfs_config PRIVATE amebad_freertos_tcp)
    target_include_directories(lfs_config PRIVATE "${CMAKE_SOURCE_DIR}/component/syscall_config")
endif()

# C++ self-test (cpp_selftest.cpp + LPUART console port). main.c calls
# amebad_console_init_lpuart() + cpp_selftest() under the same define; cpp_selftest.cpp (CXX,
# XIP-tail-pinned via liblfs_config.a) is added onto the shared lfs_config here.
if(AMEBAD_CPP_SELFTEST)
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/main.c"
                 APPEND PROPERTY COMPILE_DEFINITIONS AMEBAD_CPP_SELFTEST)
    target_compile_definitions(lfs_config PRIVATE AMEBAD_CPP_SELFTEST)
    target_sources(lfs_config PRIVATE "${CMAKE_SOURCE_DIR}/component/littlefs_config/cpp_selftest.cpp")
endif()

# Archives wrapped in --start-group (cross_refs) for circular vendor refs.
# libc (-lc) supplies __libc_init_array (KM4 has C++ global ctors; KM0 didn't)
# plus memcpy/memset — so KM4 doesn't need the ROM memcpy alias KM0 uses.
target_link_libraries(target_img2_km4 PRIVATE
    amebad_sdk_includes
    syscall_config       # OBJECT lib: cross-platform newlib retarget (VFS + malloc + byte-order), force-included so its strong defs beat libc's ENOSYS stubs
    amebad_syscall_port  # OBJECT lib: AmebaD console backend (strong at_console_default_backend -> LOGUART), also force-included
    amebad_mbedtls_rom   # INTERFACE: ROM mbedTLS 2.4.0 headers + MBEDTLS_CONFIG_FILE (no archive)
    # LittleFS (amebad_lfs_port + lfs_config + littlefs) and its flash backend
    # (amebad_mbed) join the group: littlefs is a leaf needing libc (printf/strspn)
    # + libgcc (__popcountsi2), which follow the group, and as a transitive
    # EXCLUDE_FROM_ALL lib CMake would otherwise sink it past -lc/-lgcc and leave
    # those undefined. amebad_mbed (flash_api) is lfs_port's backend. (The upstream
    # mbedcrypto/mbedx509 + picotls/ngtcp2 crypto join per-feature via the _grp vars.)
    "$<LINK_GROUP:cross_refs,FreeRTOS,freertos_port,amebad_osdep,amebad_ram_hp,amebad_ram_common,amebad_usrcfg_common,amebad_usrcfg_hp,amebad_pmc_hp,amebad_wifi,amebad_wlan_glue,amebad_freertos_tcp,freertos_plus_tcp,freertos_plus_tcp_port,amebad_wlan,amebad_wps,amebad_lfs_port,amebad_mbed,lfs_config,littlefs${_diag_grp}${_hci_grp}${_quic_grp}${_pk11_grp}${_bench_grp}${_aws_grp}>"
    m
    stdc++
    c
    gcc   # libgcc: __aeabi_uldivmod (64-bit divide in mbedTLS/ngtcp2)
)

target_link_options(target_img2_km4 PRIVATE
    "-T${_combined_ld}"
    "-L${CMAKE_CURRENT_SOURCE_DIR}"
    "-Wl,--no-enum-size-warning"
    "-Wl,--warn-common"
    "-Wl,-Map=$<TARGET_FILE_DIR:target_img2_km4>/target_img2.map"
)

# Secure-world (TF-M): when AMEBAD_TFM_BOOT is on, app_km4 IS TF-M's Non-Secure
# side. It consumes TF-M's exported NS interface (built by app_km4_secure's
# tfm_spe ExternalProject) BY PATH — a single km4 build tree cannot
# add_subdirectory the export, which only exists after tfm_spe builds. So: put its
# headers on the include path, link s_veneers.o, and depend on tfm_spe so it
# builds + installs first. tfm_ns_check.c is a minimal compile/link probe; real
# PSA calls + NS layout/boot handoff are a follow-on stage.
if(AMEBAD_TFM_BOOT)
    set(_tfm_export "${CMAKE_BINARY_DIR}/component/amebad/app_km4_secure/tfm_spe_export")
    set(_tfm_src    "${CMAKE_SOURCE_DIR}/component/trusted-firmware-m_config/trusted-firmware-m")
    add_dependencies(target_img2_km4 tfm_spe)
    # app_km4 IS the NSPE: issue a real PSA call into TF-M. Link the PSA client
    # interface (tfm_platform_ioctl -> psa_call -> dispatch -> SG veneer) + the
    # bare-metal single-threaded dispatch (PSA is called once from main() before
    # the scheduler starts). The PSA-client .c are plain TF-M source (referenced
    # directly, not via the install's curated set); s_veneers.o + generated
    # psa_manifest/sid.h come from the SPE export.
    target_sources(target_img2_km4 PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tfm_psa_selftest.c"
        "${_tfm_src}/interface/src/tfm_platform_api.c"
        "${_tfm_src}/interface/src/tfm_tz_psa_ns_api.c"
        # Serialized NS interface (mutex per PSA dispatch) + its FreeRTOS os_wrapper backend,
        # replacing the bare-metal (unserialized) dispatch. See tfm_os_wrapper_freertos.c.
        "${_tfm_src}/interface/src/os_wrapper/tfm_ns_interface_rtos.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/tfm_os_wrapper_freertos.c")
    # Minimal image: keep the PSA ping + eFuse-HUK probe (the TF-M liveness proof),
    # drop the HW-crypto KAT/GCM throughput bench inside tfm_psa_selftest.c.
    if(AMEBAD_ENABLE_MINIMAL)
        set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tfm_psa_selftest.c"
                     APPEND PROPERTY COMPILE_DEFINITIONS AT_PROJECT_MINIMAL)
    endif()
    target_include_directories(target_img2_km4 PRIVATE
        "${_tfm_export}/interface/include"
        "${_tfm_src}/interface/include")
    target_compile_definitions(target_img2_km4 PRIVATE AMEBAD_TFM_BOOT=1)
    target_link_libraries(target_img2_km4 PRIVATE
        "${_tfm_export}/interface/lib/s_veneers.o")
    # Clamp the NS FreeRTOS heap top below TF-M's secure RAM. tfm_s runs cached-XIP
    # (code in flash, not RAM), so the only secure SRAM is S_DATA
    # (0x10060000..0x1007FFFF); app_km4 as the NSPE must keep its heap (and
    # everything else) under that or os_heap_init faults the moment heap5 touches
    # the secure region. __tz_sram_top__ feeds rlx8721d_img2_is.ld's __sram_end__
    # (= region_defs.h S_DATA_START). This is the os_heap_init-hang fix: previously
    # the NS heap ran up to BD_RAM_NS top 0x1007C000, overlapping the secure window.
    target_link_options(target_img2_km4 PRIVATE
        "-Wl,--defsym=__tz_sram_top__=0x10060000")

    # PSA secure storage self-test (make STORAGE=ON). Adds the ITS [+ PS] NS client
    # interface + the storage round-trip/persistence test (tfm_storage_selftest.c).
    # The psa_its_* / psa_ps_* veneers come from s_veneers.o; the storage flag also
    # enables the partitions in the SPE (app_km4_secure passes the -D). PS sources +
    # AMEBAD_TFM_STORAGE_PS are added in Milestone B once the crypto-enabled SPE is built.
    if(AMEBAD_TFM_STORAGE)
        target_sources(target_img2_km4 PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/tfm_storage_selftest.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/tfm_storage_lock.c"   # NS RT_DEV_LOCK_FLASH/CRYPTO around secure-storage writes
            "${_tfm_src}/interface/src/tfm_its_api.c"
            "${_tfm_src}/interface/src/tfm_ps_api.c")   # Protected Storage NS client
        target_compile_definitions(target_img2_km4 PRIVATE
            AMEBAD_TFM_STORAGE=1 AMEBAD_TFM_STORAGE_PS=1)
        set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/main.c"
                     APPEND PROPERTY COMPILE_DEFINITIONS AMEBAD_TFM_STORAGE)
        # --wrap every NS PSA-storage WRITE so tfm_storage_lock.c brackets it with the NS
        # flash/crypto device mutexes (waits out any in-progress NS SPIC op before the SPE
        # programs flash). Covers all callers without editing the TF-M submodule's *_api.c.
        target_link_options(target_img2_km4 PRIVATE
            "-Wl,--wrap=psa_its_set" "-Wl,--wrap=psa_its_remove"
            "-Wl,--wrap=psa_ps_set"  "-Wl,--wrap=psa_ps_remove")
    endif()
endif()

# XIP boot-hang dodge: shift the whole .xip_image2.text block by AMEBAD_XIP_PAD
# bytes (consumed by rlx8721d_img2_is.ld via __xip_pad__) to move app_start's
# early fwlib callees off the layout-sensitive hanging address. Sweep this when
# the KM4 image hangs at boot (console stuck after the KM0 "RRAM:" line).
# Default 0: LittleFS .text is pinned to the tail of .xip_image2.text by the ld
# script (rlx8721d_img2_is.ld) so it doesn't shift the boot/WiFi code, so no pad
# is needed for the LFS image. Kept as a manual escape hatch for any future
# layout-sensitive boot hang.
set(AMEBAD_XIP_PAD "0" CACHE STRING "XIP .text pad bytes (boot-hang layout dodge)")
if(NOT AMEBAD_XIP_PAD STREQUAL "0")
    target_link_options(target_img2_km4 PRIVATE
        "-Wl,--defsym=__xip_pad__=${AMEBAD_XIP_PAD}")
endif()

target_compile_options(target_img2_km4 PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)

add_custom_command(TARGET target_img2_km4 POST_BUILD
    COMMAND "${CMAKE_SIZE}" -A --radix=16 "$<TARGET_FILE:target_img2_km4>"
    COMMAND "${CMAKE_SIZE}" -t --radix=16 "$<TARGET_FILE:target_img2_km4>"
    COMMENT "section sizes for target_img2_km4"
    VERBATIM
)

# Stage 2: split axf -> ram_2 / xip_image2 / psram_2, prepend 32-byte headers,
# cat (xip + ram_2 + psram_2), 4K pad -> km4_image2_all.bin, optional RSIP @
# the KM4 XIP cache base 0x0e000000. Helper in image_tool/.
amebad_make_image2_all_bin_hp(target_img2_km4 km4_image2_all.bin 0x0e000000)
