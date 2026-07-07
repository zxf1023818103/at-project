# ---- standalone CMake project (lib/executable split: libs in component, this image here) ----
# Built directly via `cmake -S project/amebad/app_km4 -B build/app_km4 -DCMAKE_TOOLCHAIN_FILE=
# cmake/amebad-km4.cmake` (the toolchain FORCE-sets AMEBAD_CHIP). AT_ROOT anchors the repo root so the
# add_subdirectory(component) below + every ${AT_ROOT}/... ref resolve regardless of CMAKE_SOURCE_DIR.
cmake_minimum_required(VERSION 3.20)
project(app_km4 C CXX ASM)
get_filename_component(AT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(AT_ROOT "${AT_ROOT}" CACHE INTERNAL "at-project repo root")
# The AmebaD KM4 app entry is now the SHARED cross-platform main (src/main.cpp): its AmebaD
# branch (CONFIG_PLATFORM_8721D) is the former app_km4/main.c. Compile ONLY this file (not the
# whole src/ AT framework) — the AmebaD branch never references at::AtTask etc. The feature-macro
# set_property(SOURCE ...) lines below target it via ${AT_MAIN_SRC}.
set(AT_MAIN_SRC "${AT_ROOT}/src/main.cpp")
# ROMKAT links the ROM mbedTLS 2.4.0 (ABI-incompatible with upstream mbedcrypto), so fs_cli's
# md5/sha*sum are compiled out. Tell monitor_config before it is added (AMEBAD_ENABLE_ROM_KAT is
# a -D cache var, so it is already set here even though the option() default is declared later).
if(AMEBAD_ENABLE_ROM_KAT)
    set(AT_FS_CLI_HASH OFF)
endif()
add_subdirectory(${AT_ROOT}/component ${CMAKE_BINARY_DIR}/component)
# __FILE__ basename for this app's logs (moved from the deleted root CMakeLists).
add_compile_options("-fmacro-prefix-map=${CMAKE_CURRENT_LIST_DIR}/=")
add_compile_options("-fmacro-prefix-map=${AT_ROOT}/src/=")   # the shared main is now src/main.cpp -> basename in [KM4] logs

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
        "${AT_MAIN_SRC}"
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
set(_rom_mbed_ld "${AT_ROOT}/component/amebad/mbedtls_rom/mbedtls_rom.ld")  # ROM mbedTLS 2.4.0 + ssl ram map symbols
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
    "${AT_MAIN_SRC}"
    "${CMAKE_CURRENT_SOURCE_DIR}/cm_backtrace_stub.c"   # weak no-op (CmBacktrace lib not in this checkout)
    "${CMAKE_CURRENT_SOURCE_DIR}/minimal_stubs.c"       # weak CRYPTO_Init / rtw_if_wifi_thread (no TLS/WiFi)
    "${CMAKE_CURRENT_SOURCE_DIR}/wifi_scan_compat.c"    # weak wifi_mode + connect/EAP/promisc stubs
    "${CMAKE_CURRENT_SOURCE_DIR}/freertos_tcp_compat.c" # weak lwip stubs (lwip IP not linked; FreeRTOS+TCP instead)
)
# (lfs_selftest moved into the lfs_port archive so ALL LittleFS .text is
# pinned to the XIP tail by the ld script — keeps boot/WiFi code addresses stable.)

# ROM-mbedTLS self-test image (make ROMKAT=ON): builds the ROM mbedTLS 2.4.0 KAT +
# ROM-vs-HW AES compare boot self-tests (rom_mbedtls_kat.c / rom_hwcrypto_test.c /
# rom_mbedtls.c). Default OFF: the console images instead link upstream Mbed TLS
# (mbedcrypto) for fs_cli's md5sum/sha*sum, which is ABI-incompatible with the ROM
# mbedTLS (same symbol names) — so the two are mutually exclusive (guarded below).
option(AMEBAD_ENABLE_ROM_KAT "Build the ROM mbedTLS 2.4.0 / HW-crypto boot self-tests (no upstream mbedTLS)" OFF)

# Console commands registered from main() (like aws_cli.c), so the shared monitor stays
# untouched: filesystem (ls/cat/rm/rmdir/mkdir/mv/cp/touch/hexdump/df/mkfs/chmod) over the
# POSIX VFS, and time/SNTP (date/uptime/sntp_update). KM4-only; present in every console
# image. The QUIC image drops the monitor AND freertos_cli, so exclude these there too (else
# FreeRTOS_CLIRegisterCommand is an undefined ref). lfs_config.h / lfs.h come from the
# lfs_config + littlefs link-group members (incl. LFS_THREADSAFE); FreeRTOS_CLI.h needs the
# explicit include, same as the AWS block below. time_cli.c's sntp_sync_once() is a weak
# stub unless sntp_time.c (make SNTP=ON) provides the strong def.
if(NOT AMEBAD_ENABLE_QUIC)
    # fs/env/time_cli moved to the shared component/monitor_config (monitor_cmds, in _diag_grp
    # below); wifi_cli (wifi forget + resolve/ip6) stays AmebaD-local. All still registered from
    # main() after monitor_start(). FS_CLI_HASH now gated in monitor_config via AT_FS_CLI_HASH.
    target_sources(target_img2_km4 PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/wifi_cli.c")
    target_include_directories(target_img2_km4 PRIVATE
        "${AT_ROOT}/component/freertos-cli_config/freertos-cli")
    # env.c (syscall_config) persists every setenv() to KV/<name> via this link-time wrap; main.c
    # mounts LittleFS + env_init()s globally now, so the WIFI_* / DHCP_* / NTP_SERVER env vars work
    # and persist in EVERY non-QUIC image (not just AWS). __real_setenv resolves to libc's setenv.
    target_link_options(target_img2_km4 PRIVATE "-Wl,--wrap=setenv" "-Wl,--wrap=unsetenv")
endif()

# minizip (ZIP archive) console commands (zip/unzip over the LittleFS VFS) — DEMO only. Built
# into the console images EXCEPT QUIC (no console) and AWS (heap/flash-tight: ~90 KB free with
# TLS/MQTT up, and minizip's ~50 KB working set risks OOM on a re-handshake spike). So it rides
# the lighter images (default / SNTP / MINIMAL), 200 KB+ free heap. minizip + zlibstatic are leaf
# libs needing libc/libgcc, so (like littlefs) they join the cross_refs LINK_GROUP via _mz_grp
# below to avoid CMake sinking the EXCLUDE_FROM_ALL libs past -lc. zip.h/unzip.h/ioapi.h come
# from minizip's PUBLIC include via the group.
set(_mz_grp "")
if(NOT AMEBAD_ENABLE_QUIC AND NOT AMEBAD_ENABLE_AWS_IOT)
    # zip_cli.c moved to component/monitor_config (target zip_cmd, which links minizip + zlibstatic).
    # All three are leaf/EXCLUDE_FROM_ALL libs needing libc/libgcc, so they ride the cross_refs group.
    set(_mz_grp ",zip_cmd,minizip,zlibstatic")
    # zlib's zcalloc + minizip's ALLOC bottom out in libc malloc, which on AmebaD uses newlib's
    # tiny 4 KB _sbrk arena — far too small for deflate (~tens of KB) / minizip's 64 KB buffer
    # (faults inside deflate). Redirect every libc allocation in this image to the FreeRTOS/TLSF
    # heap (~270 KB) via the __wrap_* malloc family in syscall_config/newlib_syscalls.c (always
    # linked into the KM4 image). Link-time, so it also catches the allocations inside zlibstatic /
    # minizip.
    target_link_options(target_img2_km4 PRIVATE
        "-Wl,--wrap=malloc" "-Wl,--wrap=free" "-Wl,--wrap=calloc" "-Wl,--wrap=realloc")
    set_property(SOURCE "${AT_MAIN_SRC}"
                 APPEND PROPERTY COMPILE_DEFINITIONS AMEBAD_ENABLE_MINIZIP)
endif()

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
    set_property(SOURCE "${AT_MAIN_SRC}"
                 APPEND PROPERTY COMPILE_DEFINITIONS AT_PROJECT_MINIMAL)
endif()
# AWS IoT Core MQTT/TLS demo (make AWS=ON): replaces the iperf diagnostics with an MQTT
# client (coreMQTT over the mbedTLS TLS transport). Like QUIC/PKCS11 it drops the iperf +
# ROM-mbedTLS KAT TUs (it links the upstream mbedTLS, ABI-incompatible with the ROM one).
# AWS=ON implies STORAGE=ON (the private key lives in TF-M Protected Storage). Declared here
# so the diag gate below can see it.
option(AMEBAD_ENABLE_AWS_IOT "Build the AWS IoT Core MQTT/TLS demo (coreMQTT + mbedTLS; key in TF-M PS)" OFF)

# coreSNTP UTC time-sync is now default-on (linked in every non-QUIC image; see below) - no flag.

# Low-memory network profile for the connect-only images (AWS / MINIMAL): shrink the
# FreeRTOS-Plus-TCP per-socket + descriptor buffers (FreeRTOSIPConfig.h) and the WiFi skb pool
# (rtw_opt_skbuf.c) — they are sized for the iperf/QUIC throughput tests and waste ~100 KB of the
# tight NS heap on a low-rate MQTT client. Define on the two shared targets that own those configs
# (they exist by now — added under component/ before app_km4). Throughput images keep the big sizes.
if(AMEBAD_ENABLE_AWS_IOT OR AMEBAD_ENABLE_MINIMAL)
    foreach(_net_tgt freertos_plus_tcp amebad_wlan_glue)
        if(TARGET ${_net_tgt})
            target_compile_definitions(${_net_tgt} PRIVATE AMEBAD_NET_LOW_MEM)
        endif()
    endforeach()
endif()

# ROM-mbedTLS crypto self-tests (make ROMKAT=ON). They reference ROM mbedTLS symbols
# (mbedtls_sha*/md5/aes_*) with the ROM ABI and define mbedtls_calloc/free, which collide with
# the upstream mbedcrypto every console image now links for fs_cli's md5sum/sha*sum. So ROMKAT is
# its own image: ROM mbedTLS, no upstream, no hash commands (fs_cli's FS_CLI_HASH is off). main.c
# calls rom_mbedtls_kat() under AMEBAD_ROM_KAT.
if(AMEBAD_ENABLE_ROM_KAT AND (AMEBAD_ENABLE_QUIC OR AMEBAD_ENABLE_AWS_IOT OR AMEBAD_ENABLE_PKCS11
   OR AMEBAD_ENABLE_CRYPTOBENCH OR AMEBAD_ENABLE_NIMBLE))
    message(FATAL_ERROR "ROMKAT=ON (ROM mbedTLS self-tests) cannot combine with an upstream-mbedTLS image (AWS/QUIC/PKCS11/BENCH/NIMBLE)")
endif()
if(AMEBAD_ENABLE_ROM_KAT)
    target_sources(target_img2_km4 PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/rom_mbedtls_kat.c"     # known-answer test of the ROM mbedTLS 2.4.0 software crypto
        "${AT_ROOT}/component/amebad/mbedtls_rom/rom_mbedtls.c"  # ROM mbedTLS ram-map glue (use_hw_crypto_func=0)
        "${CMAKE_CURRENT_SOURCE_DIR}/rom_hwcrypto_test.c"   # HW-vs-SW compare of rom_ssl_ram_map.h hw_crypto_* (IPSEC engine)
    )
    set_property(SOURCE "${AT_MAIN_SRC}"
                 APPEND PROPERTY COMPILE_DEFINITIONS AMEBAD_ROM_KAT)
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
# mcumgr SMP-over-BLE DFU (img_mgmt -> ota_flash + os_mgmt echo/reset). Implies NIMBLE (forced ON
# by the Makefile). Adds the mcumgr core + the smp_bt transport + the img/os platform impls.
option(AMEBAD_ENABLE_MCUMGR "Build mcumgr SMP-over-BLE DFU (implies NIMBLE)" OFF)

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
    set(_hci_root "${AT_ROOT}/component/amebad/hci")
    if(AMEBAD_HCI_FW_MP)
        set(_hci_fw_src "${_hci_root}/hci_mp_patch.c")
    else()
        set(_hci_fw_src "${_hci_root}/hci_normal_patch.c")
    endif()
    set_source_files_properties("${_hci_fw_src}" PROPERTIES
        COMPILE_OPTIONS "-Wno-unused-variable;-Wno-unused-const-variable")
    target_sources(target_img2_km4 PRIVATE
        "${_hci_fw_src}")
    # hci_board.c's bt_get_mac_address() reads the BLE MAC via efuse_logical_read (0x190),
    # which lives in efuse_api.c (not built into amebad_mbed). AWS images already add it
    # below; add it here for the HCI/NIMBLE images so the controller MAC read resolves.
    if(NOT AMEBAD_ENABLE_AWS_IOT)
        target_sources(target_img2_km4 PRIVATE
            "${AT_ROOT}/component/amebad/mbed/efuse_api.c")
        set_source_files_properties("${AT_ROOT}/component/amebad/mbed/efuse_api.c"
            PROPERTIES COMPILE_OPTIONS "-w")
    endif()
endif()
if(AMEBAD_ENABLE_NIMBLE)
    # NimBLE SM crypto now rides upstream mbedTLS (mbedtls_sm_glue.c), not wolfCrypt:
    # The mbedtls_config umbrella + its archives (crypto/x509/tls + port HW-ALT/entropy) join the
    # group. All of mbedtls_config's in-group deps are listed so no member resolves group-external
    # (LINK_GROUP needs real archive names — the umbrella INTERFACE wouldn't expand inside it).
    set(_hci_grp ",amebad_hci,nimble,nimble_config,mbedcrypto,mbedx509,mbedtls,mbedtls_port,mbedtls_config")
    set_property(SOURCE "${AT_MAIN_SRC}"
                 APPEND PROPERTY COMPILE_DEFINITIONS AT_PROJECT_NIMBLE)
elseif(AMEBAD_ENABLE_HCI)
    set_property(SOURCE "${AT_MAIN_SRC}"
                 APPEND PROPERTY COMPILE_DEFINITIONS AT_PROJECT_HCI)
endif()

# mcumgr SMP-over-BLE DFU. MCUMGR implies NIMBLE (so the block above already added the NimBLE +
# mbedTLS archives + AT_PROJECT_NIMBLE). Append the mcumgr core (`mcumgr`), the BLE transport
# (`mcumgr_port`) and tinycbor to the link group (real archive names; the _config umbrellas don't
# expand inside LINK_GROUP), link mcumgr_config separately for header propagation, and pull in
# ota_flash.c (the slot writer; normally AWS-only) as the DFU flash sink.
if(AMEBAD_ENABLE_MCUMGR)
    set(_hci_grp "${_hci_grp},mcumgr,mcumgr_port,tinycbor")
    # Header propagation only (the archives ride the LINK_GROUP above); linking the mcumgr_config
    # umbrella here would form a dependency cycle with the group. mcumgr's interface includes carry
    # the mgmt/img/os headers + the tinycbor/ prefix symlink that smp_img_ota.c needs.
    target_include_directories(target_img2_km4 PRIVATE
        $<TARGET_PROPERTY:mcumgr,INTERFACE_INCLUDE_DIRECTORIES>)
    target_sources(target_img2_km4 PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/smp_img_ota.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/smp_fs.c")     # fs_mgmt impl over the LittleFS POSIX VFS
    set_property(SOURCE "${AT_MAIN_SRC}"
                 APPEND PROPERTY COMPILE_DEFINITIONS AT_PROJECT_MCUMGR
                 "AMEBAD_MCUMGR_FW_VERSION=\"${AMEBAD_FW_VERSION}\"")
    if(NOT AMEBAD_ENABLE_AWS_IOT)
        target_sources(target_img2_km4 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ota_flash.c")
    endif()
endif()

# QUIC self-test (ngtcp2 + picotls TLS 1.3 on the mbedTLS/PSA backend — the wolfSSL
# replacement). ngtcp2 + the picotls binding + picotls + mbedcrypto/x509 + the mbedTLS
# FreeRTOS-threading/entropy glue join the cross_refs group; they are mutually referenced.
# AT-command / monitor shell libs: linked by default, dropped from the QUIC
# image (shell init is gated off in main.c under AT_PROJECT_QUIC). Removing them
# changes the KM4 image2 XIP relative layout (boot-hang dodge) and shrinks it.
set(_diag_grp ",monitor,monitor_port,monitor_cmds,freertos_cli")

set(_quic_grp "")
if(AMEBAD_ENABLE_QUIC)
    set(_diag_grp "")
    # The hand-tuned flat --start-group needs real archive names (CMake won't expand the
    # ngtcp2_config / picotls_config INTERFACE aggregates inside a LINK_GROUP), so list the
    # members. ngtcp2_crypto_picotls (picotls binding) replaces ngtcp2_crypto (wolfSSL).
    set(_quic_grp ",ngtcp2_crypto_picotls,ngtcp2_static,picotls_core,picotls_mbedtls,mbedcrypto,mbedx509,mbedtls,mbedtls_port,mbedtls_config")
    target_sources(target_img2_km4 PRIVATE
        "${AT_ROOT}/component/quic_selftest_config/quic_test.c")   # shared QUIC self-test (moved out of app_km4);
                                                                  # compiled here as a direct source (byte-identical
                                                                  # to before) to keep the XIP-layout-sensitive image

    set_property(SOURCE "${AT_MAIN_SRC}"
                 APPEND PROPERTY COMPILE_DEFINITIONS AT_PROJECT_QUIC)
endif()

# corePKCS11 token self-test (AMEBAD_ENABLE_PKCS11): the software PKCS#11 token over the
# upstream mbedTLS backend (component/amebad/mbedtls_port) with object storage on LittleFS — the
# reused upstream POSIX PAL (fopen/remove) over the AmebaD newlib VFS. corePKCS11 + mbedcrypto
# /mbedx509 + the mbedTLS FreeRTOS-threading/entropy glue join the cross_refs group (real
# archive names; the _config/umbrella INTERFACEs don't expand inside LINK_GROUP).
set(_pk11_grp "")
if(AMEBAD_ENABLE_PKCS11)
    # pkcs11_selftest.c + the fopen POSIX PAL moved to the shared component/pkcs11_selftest_config
    # (target pkcs11_selftest). Its archive rides the cross_refs group alongside corepkcs11/mbedcrypto
    # /mbedx509. main() auto-runs pkcs11_selftest_run() under AT_PROJECT_PKCS11 (the `pkcs11` console
    # command in the shared lib is unused here — this image auto-runs it instead).
    set(_pk11_grp ",pkcs11_selftest,corepkcs11,mbedcrypto,mbedx509,mbedtls,mbedtls_port,mbedtls_config")
    set_property(SOURCE "${AT_MAIN_SRC}"
                 APPEND PROPERTY COMPILE_DEFINITIONS AT_PROJECT_PKCS11)
endif()

# Crypto microbenchmark (AMEBAD_ENABLE_CRYPTOBENCH): the [MCB] scores for upstream Mbed TLS
# v3.6.4 crypto (SHA/AES/GCM/ChaChaPoly + ECDSA/ECDHE/X25519/RSA), the apples-to-apples successor
# to the removed wolfCrypt [WCB] bench. The bench itself moved to the shared cross-platform
# component/mbedtls_bench_config (target mbedtls_bench, with a bench_timer_* seam — ARM SysTick
# here, RISC-V mcycle on bl602). Its archive + mbedcrypto + the FreeRTOS-threading/entropy glue
# join the cross_refs group (real archive names; the _config umbrellas don't expand in LINK_GROUP).
# main() auto-runs mbedtls_bench() under AT_PROJECT_CRYPTOBENCH (the `bench` console command in the
# shared lib is unused here — this image auto-runs it instead).
set(_bench_grp "")
if(AMEBAD_ENABLE_CRYPTOBENCH)
    set(_bench_grp ",mbedtls_bench,mbedcrypto,mbedx509,mbedtls,mbedtls_port,mbedtls_config")
    set_property(SOURCE "${AT_MAIN_SRC}"
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
    # TLS backend is switchable (TLS_BACKEND=mbedtls|picotls, threaded from the Makefile as
    # AMEBAD_TLS_BACKEND; default mbedtls). Both transports export the same TLS_FreeRTOS_* API and
    # NetworkCredentials_t, so aws_iot_demo.c (via transport_tls.h) is identical either way. picotls's
    # picotls_core + picotls_mbedtls join the circular mbedTLS archives in the group (its crypto rides
    # PSA/mbedcrypto); the network_transport_* libs are one-directional so they link as ordinary libs.
    if(AMEBAD_TLS_BACKEND STREQUAL "fallback")
        # Both backends linked; transport_fallback.c tries picotls (1.3) then mbedTLS (1.2/1.3).
        # The group already carries the picotls + mbedTLS archives (picotls rides mbedcrypto anyway).
        set(_aws_grp ",picotls_core,picotls_mbedtls,mbedcrypto,mbedx509,mbedtls,mbedtls_port,mbedtls_config")
        target_link_libraries(target_img2_km4 PRIVATE
            core_mqtt backoff_algorithm
            network_transport_fallback network_transport_picotls network_transport_mbedtls
            network_transport_mbedtls_bio network_transport_sockets)
    elseif(AMEBAD_TLS_BACKEND STREQUAL "picotls")
        set(_aws_grp ",picotls_core,picotls_mbedtls,mbedcrypto,mbedx509,mbedtls,mbedtls_port,mbedtls_config")
        target_link_libraries(target_img2_km4 PRIVATE
            core_mqtt backoff_algorithm
            network_transport_picotls network_transport_sockets)
    else()
        set(_aws_grp ",mbedcrypto,mbedx509,mbedtls,mbedtls_port,mbedtls_config")
        target_link_libraries(target_img2_km4 PRIVATE
            core_mqtt backoff_algorithm
            network_transport_mbedtls network_transport_mbedtls_bio network_transport_sockets)
    endif()
    # Compressed CA root bundle (CRT_BUNDLE=ON): verify the server cert against the embedded
    # Mozilla root bundle (esp_crt_bundle, ~121 roots) instead of the single pinned root in
    # NetworkCredentials_t.pRootCa. esp_crt_bundle references mbedcrypto/mbedx509 (also in the
    # group), so it joins the cross_refs group; linking esp_crt_bundle_config gives crt_bundle_cli.c
    # + the app TUs the esp_crt_bundle.h include path. The AMEBAD_ENABLE_CRT_BUNDLE macro gates the
    # creds.useCrtBundle assignments in aws_iot_demo.c / ota_http.c / ota_app.c and the transport
    # hooks (the transport libs get the macro from component/network_transport).
    if(AMEBAD_ENABLE_CRT_BUNDLE STREQUAL "ON")
        # crt_bundle_cli.c + esp_crt_bundle moved to the shared component/esp_crt_bundle_config. The
        # `crt_bundle` command now rides the cross_refs group as the crt_bundle_cli archive (alongside
        # esp_crt_bundle/mbedx509/mbedcrypto); crt_bundle_cli sees network_transport at configure time
        # so its `connect` subcommand (CRT_BUNDLE_HAVE_TLS_TRANSPORT) is compiled in. main() registers
        # it (crt_bundle_cli_register). esp_crt_bundle_config is still linked for the esp_crt_bundle.h
        # include path on aws_iot_demo.c / ota_http.c (creds.useCrtBundle).
        set(_aws_grp "${_aws_grp},crt_bundle_cli,esp_crt_bundle")
        target_link_libraries(target_img2_km4 PRIVATE esp_crt_bundle_config)
        target_compile_definitions(target_img2_km4 PRIVATE AMEBAD_ENABLE_CRT_BUNDLE)
    endif()
    target_sources(target_img2_km4 PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/aws_iot_demo.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/aws_platform.c"             # aws_get_mac (efuse) + `aws blemac/secchk` + TF-M pstest/xiptest
        "${CMAKE_CURRENT_SOURCE_DIR}/ota_http.c"                  # `ota_http <url>` command (coreHTTP + llhttp)
        "${CMAKE_CURRENT_SOURCE_DIR}/ota_flash.c"                 # runtime OTA slot write/toggle/reboot
        # AWS IoT OTA Agent over the shared coreMQTT-Agent: the agent foundation (queue/pool/
        # subscription dispatch), the OTA PAL (ota_flash + mbedTLS code-sign verify), and the OTA
        # app wiring (OtaInterfaces_t + OTA_EventProcessingTask). The OTA lib (aws_ota) already
        # builds ota_mqtt.c + ota_os_freertos.c into its archive, so only these app/port TUs are listed.
        "${CMAKE_CURRENT_SOURCE_DIR}/freertos_agent_message.c"    # MQTTAgentMessageInterface_t over a FreeRTOS queue
        "${CMAKE_CURRENT_SOURCE_DIR}/freertos_command_pool.c"     # static MQTTAgentCommand_t pool
        "${CMAKE_CURRENT_SOURCE_DIR}/subscription_manager.c"      # topic-filter -> incoming-publish dispatch
        "${CMAKE_CURRENT_SOURCE_DIR}/ota_pal.c"                   # OTA PAL (slot write + signature verify)
        "${CMAKE_CURRENT_SOURCE_DIR}/ota_app.c"                   # OTA agent wiring (OTA_Init + task)
        "${CMAKE_CURRENT_SOURCE_DIR}/urlparse.c"                  # vendored URL parser (from ngtcp2)
        "${AT_ROOT}/component/amebad/mbed/efuse_api.c")   # efuse_logical_read (BLE MAC @ 0x190); not built into amebad_mbed
    set_source_files_properties("${AT_ROOT}/component/amebad/mbed/efuse_api.c"
        PROPERTIES COMPILE_OPTIONS "-w")
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/urlparse.c"
        PROPERTIES COMPILE_OPTIONS "-w")                          # vendored 3rd-party: silence its warnings
    # coreHTTP (request builder) + its bundled llhttp parser, for ota_http.c.
    target_link_libraries(target_img2_km4 PRIVATE corehttp_config)
    target_include_directories(target_img2_km4 PRIVATE
        "${AT_ROOT}/component/corehttp_config"
        "${AT_ROOT}/component/corehttp_config/corehttp/source/include"
        "${AT_ROOT}/component/corehttp_config/corehttp/source/interface"
        "${AT_ROOT}/component/corehttp_config/corehttp/source/dependency/3rdparty/llhttp/include")
    # coreMQTT-Agent (thread-safe wrapper that lets the telemetry + OTA tasks share one MQTT
    # connection) + the AWS IoT OTA Agent. Both are one-directional: coremqtt_agent calls
    # coreMQTT, aws_ota reaches MQTT only through the consumer-supplied OtaMqttInterface_t — so
    # they topo-sort ahead of the mbedTLS cross_refs group and link as ordinary libs. The OTA
    # PAL's mbedTLS signature verify (x509/pk) is already satisfied by the _aws_grp archives.
    target_link_libraries(target_img2_km4 PRIVATE coremqtt-agent_config ota_config)
    target_include_directories(target_img2_km4 PRIVATE
        "${AT_ROOT}/component/coremqtt-agent_config"
        "${AT_ROOT}/component/coremqtt-agent_config/coremqtt-agent/source/include"
        "${AT_ROOT}/component/ota_config"
        "${AT_ROOT}/component/ota_config/ota/source/include"
        "${AT_ROOT}/component/ota_config/ota/source/portable/os")
    # The OTA lib (ota_mqtt.c stringBuilder) is the only caller of strncat in this image; newlib's
    # strncat is not otherwise pulled, so resolve it to the ROM _strncat (string_rom_km4.ld),
    # mirroring the global memcpy=_memcpy romalias above.
    target_link_options(target_img2_km4 PRIVATE "-Wl,--defsym=strncat=_strncat")
    # FreeRTOS-Plus-CLI header for aws_platform.c (FreeRTOS_CLIRegisterCommand); the freertos_cli
    # archive is already in the link group via _diag_grp.
    target_include_directories(target_img2_km4 PRIVATE
        "${AT_ROOT}/component/freertos-cli_config/freertos-cli")
    # Shared AWS provisioning + `aws` console command (component/aws_iot_config aws_provision_core).
    # The client-key backend is the TF-M ITS impl (AMEBAD_TFM_STORAGE) or the plain-file impl, compiled
    # straight into the image (the ITS one needs the PSA/ITS headers this TF-M build already has).
    # AWS_KEY_MAX 1536 (heap-tuned, vs the 2560 default) on the shared lib + the AWS app TUs.
    target_link_libraries(target_img2_km4 PRIVATE aws_provision_core)
    target_include_directories(target_img2_km4 PRIVATE "${AT_ROOT}/component/aws_iot_config")
    target_compile_definitions(aws_provision_core PRIVATE AWS_KEY_MAX=1536)
    target_compile_definitions(target_img2_km4 PRIVATE AWS_KEY_MAX=1536)
    if(AMEBAD_TFM_STORAGE)
        target_sources(target_img2_km4 PRIVATE "${AT_ROOT}/component/aws_iot_config/aws_secret_store_its.c")
    else()
        target_sources(target_img2_km4 PRIVATE "${AT_ROOT}/component/aws_iot_config/aws_secret_store_file.c")
    endif()
    # (--wrap=setenv + the LittleFS mount + env_init are now global, in the NOT-QUIC block above.)
    set_property(SOURCE "${AT_MAIN_SRC}"
                 APPEND PROPERTY COMPILE_DEFINITIONS AT_PROJECT_AWS_IOT)
    # OTA test/version stamp: the firmware version string printed at task start + published in
    # telemetry (aws_iot_demo.c). Override per build: `make AWS=ON FW_VERSION=ota-B` (threaded to
    # -DAMEBAD_FW_VERSION by project/amebad/Makefile). Lets a bank-swapping OTA be verified by the
    # running version string.
    set(AMEBAD_FW_VERSION "ota-A" CACHE STRING "Firmware version string stamped into the AWS image")
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/aws_iot_demo.c"
                 APPEND PROPERTY COMPILE_DEFINITIONS "AT_FW_VERSION=\"${AMEBAD_FW_VERSION}\"")
    # OTA self-test version (appFirmwareVersion build number, ota_app.c). The OTA library commits the
    # new image only if its version is HIGHER than the running one, so an OTA TARGET build must bump
    # this above the running image. Override: `make AWS=ON APP_BUILD=2` (-> -DAMEBAD_APP_BUILD).
    set(AMEBAD_APP_BUILD "1" CACHE STRING "appFirmwareVersion build number for the OTA self-test")
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/ota_app.c"
                 APPEND PROPERTY COMPILE_DEFINITIONS "APP_VERSION_BUILD=${AMEBAD_APP_BUILD}")
    # AWS is a connect-and-run image: strip the boot diagnostics like MINIMAL — iperf / LFS
    # self-test / ROM-mbedTLS KAT and the TF-M HW-crypto KAT/GCM throughput bench (~40 s before
    # WiFi). The endpoint + client cert live in LittleFS (env var + plain file); only the private
    # key is in TF-M ITS (see aws_provision.c). main.c mounts LittleFS directly in the AWS arm
    # (verified not to hang in this image's layout), so AT_PROJECT_MINIMAL here only strips the
    # heavy diagnostics — lfs_selftest is skipped (the LFS mount is done by the AWS block instead).
    set_property(SOURCE "${AT_MAIN_SRC}"
                 APPEND PROPERTY COMPILE_DEFINITIONS AT_PROJECT_MINIMAL)
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tfm_psa_selftest.c"
                 APPEND PROPERTY COMPILE_DEFINITIONS AT_PROJECT_MINIMAL)
endif()

# main.c's WiFi reconnect monitor (wifi_monitor_thread) uses backoffAlgorithm, so link it in EVERY
# build - the SNTP block just below + the AWS block further down also pull it, but those are
# non-QUIC / AWS-only, and the monitor is compiled unconditionally.
target_link_libraries(target_img2_km4 PRIVATE backoffalgorithm_config)

# Shared WiFi provisioning + reconnect policy (component/wifi_service_config). main() calls
# wifi_service_start(); the amebad wifi_port_* HAL + event routing live in src/main.cpp.
target_link_libraries(target_img2_km4 PRIVATE wifi_service)

# Shared startup helpers (component/app_startup_config): app_env_time_init / app_console_start /
# app_on_network_up, called from main() + the network-up hook. Linked unconditionally (even the QUIC
# image, which drops the console libs, still calls app_on_network_up from its hook). Placed BEFORE the
# --start-group below so its console-TU references into monitor/monitor_cmds/freertos_cli (which live in
# _diag_grp inside the group) resolve; the network TU (app_startup_net.c) is self-contained.
target_link_libraries(target_img2_km4 PRIVATE app_startup)

# coreSNTP UTC time-sync is DEFAULT-ON in every non-QUIC image: plain SNTPv4 over UDP/FreeRTOS-Plus-
# TCP feeding newlib's wall clock (time_base.c via amebad_set_unix_time). coreSNTP is a leaf — it
# reaches the network only through the UdpTransportInterface_t callbacks — so it links as an ordinary
# lib (no --start-group). No TLS / TF-M needed (auth-less SNTP). main.c's network-up hook starts the
# async client (sntp_sync_start). Excluded only from QUIC (no console + XIP-layout-sensitive).
if(NOT AMEBAD_ENABLE_QUIC)
    # sntp_time.c moved to the shared component/sntp_time_config (target sntp_time; it links
    # coreSNTP + backoffAlgorithm + rxi_log transitively). time_selftest.c + rtc_time.c stay
    # AmebaD-local — they provide the strong rtc_time_store / time_selftest_run weak-hooks sntp_time calls.
    target_link_libraries(target_img2_km4 PRIVATE sntp_time)
    target_sources(target_img2_km4 PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/time_selftest.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/rtc_time.c")   # mirror SNTP UTC into the HW RTC + seed at boot (rtc_api in amebad_mbed)
endif()

# POSIX-fd VFS self-test (vfs_selftest, gated by the option in component/amebad/CMakeLists.txt).
# main.c calls it; the impl is in the shared lfs_config's lfs_selftest.c (#ifdef AMEBAD_VFS_SELFTEST).
# app_km4 is the consumer that owns the option, so per littlefs_config's design it enables the impl
# there + attaches the AmebaD specifics vfs_selftest needs (FreeRTOS+TCP sockets, syscall_config's
# syscalls.h) — kept out of the shared lfs_config / the mid-tree lfs_port library.
if(AMEBAD_VFS_SELFTEST)
    set_property(SOURCE "${AT_MAIN_SRC}"
                 APPEND PROPERTY COMPILE_DEFINITIONS AMEBAD_VFS_SELFTEST)
    target_compile_definitions(lfs_config PRIVATE AMEBAD_VFS_SELFTEST)
    target_link_libraries(lfs_config PRIVATE amebad_freertos_tcp)
    target_include_directories(lfs_config PRIVATE "${AT_ROOT}/component/syscall_config")
endif()

# C++ self-test (cpp_selftest.cpp + LPUART console port). main.c calls
# amebad_console_init_lpuart() + cpp_selftest() under the same define; cpp_selftest.cpp (CXX,
# XIP-tail-pinned via liblfs_config.a) is added onto the shared lfs_config here.
if(AMEBAD_CPP_SELFTEST)
    set_property(SOURCE "${AT_MAIN_SRC}"
                 APPEND PROPERTY COMPILE_DEFINITIONS AMEBAD_CPP_SELFTEST)
    target_compile_definitions(lfs_config PRIVATE AMEBAD_CPP_SELFTEST)
    target_sources(lfs_config PRIVATE "${AT_ROOT}/component/littlefs_config/cpp_selftest.cpp")
endif()

# Secure-boot (Ed25519) self-test (make SBOOT=ON). build_image_all.py signs the KM4 image2 in the
# boot ROM's SBOOT format; sboot_keygen.py emits build/sboot/sboot_pk.h (the matching public key).
# sboot_cli.c registers the `sboot` console command, which re-runs the boot ROM's
# ed25519_verify_signature over the running signed image2. AMEBAD_SBOOT gates both sboot_cli.c and
# the registration call in main.c.
if(AMEBAD_SBOOT)
    target_sources(target_img2_km4 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/sboot_cli.c")
    target_include_directories(target_img2_km4 PRIVATE "${AT_ROOT}/build/sboot")
    target_compile_definitions(target_img2_km4 PRIVATE AMEBAD_SBOOT)
endif()

# WPS connect test (make WPS=ON): wps_cli.c registers the `wps` console command (cmd_wps-style), driving
# the SDK WPS glue wifi_wps_config.c (wps_start). amebad_wpa_supplicant gives that glue its include
# surface (utils/os.h, wps/wps_defs.h, ...); the WPS protocol core is lib_wps.a (amebad_wps, already in
# the link group). Gates the sources, the supplicant includes, and the main.c registration.
if(AMEBAD_ENABLE_WPS)
    target_sources(target_img2_km4 PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/wps_cli.c"
        "${AT_ROOT}/component/amebad/wpa_supplicant/wpa_supplicant/wifi_wps_config.c")
    target_link_libraries(target_img2_km4 PRIVATE amebad_wpa_supplicant)
    # src root so wifi_wps_config.c's "utils/os.h" / "wps/wps_defs.h" resolve (INTERFACE only has the subdirs);
    # the SDK osdep root resolves os.h's "freertos/wrapper.h" (supplicant util was never compiled in-tree).
    target_include_directories(target_img2_km4 PRIVATE
        "${AT_ROOT}/component/amebad/wpa_supplicant/src"
        "${AT_ROOT}/platform/ameba-rtos-d/component/common/drivers/wlan/realtek/src/osdep")
    target_compile_definitions(target_img2_km4 PRIVATE AMEBAD_ENABLE_WPS)
    # Shorten __FILE__ in wifi_wps_config.c's at_log output to the basename (its printf were converted to
    # log_*; without this map the rxi_log line shows the full absolute path, unlike the other sources).
    set_property(SOURCE "${AT_ROOT}/component/amebad/wpa_supplicant/wpa_supplicant/wifi_wps_config.c"
        APPEND PROPERTY COMPILE_OPTIONS
        "-fmacro-prefix-map=${AT_ROOT}/component/amebad/wpa_supplicant/wpa_supplicant/=")
endif()

# DEBUG eFuse provisioning aids (make EFUSE_DEBUG=ON, never ship): the `efuse_burn` console command
# + the boot-time dump of all eFuse keys + MACs (tfm_psa_selftest.c, gated on AMEBAD_EFUSE_DEBUG).
# The burn command needs the TF-M secure world (EFUSE_WRITE IOCTL).
if(AMEBAD_EFUSE_DEBUG)
    target_compile_definitions(target_img2_km4 PRIVATE AMEBAD_EFUSE_DEBUG)
    if(AMEBAD_TFM_STORAGE)
        target_sources(target_img2_km4 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/efuse_burn_cli.c")
    endif()
endif()

# Upstream Mbed TLS for fs_cli's md5sum/sha*sum (component/mbedtls_config). Only the
# console images that don't already pull mbedcrypto via a feature _grp need it (default /
# MINIMAL / SNTP / HCI-alone); AWS/PKCS11/BENCH/NIMBLE list it in their own _grp, and QUIC
# has no fs_cli, and ROMKAT links ROM mbedTLS instead. Static-archive selection is
# per-member, so an image that only calls hashes pulls just md5/sha*/platform_util — no
# x509/tls/threading/port. Joins the cross_refs group (real archive name; the _config
# umbrella INTERFACE wouldn't expand inside LINK_GROUP).
set(_hash_grp "")
if(NOT AMEBAD_ENABLE_QUIC AND NOT AMEBAD_ENABLE_ROM_KAT
   AND NOT AMEBAD_ENABLE_PKCS11 AND NOT AMEBAD_ENABLE_CRYPTOBENCH
   AND NOT AMEBAD_ENABLE_AWS_IOT AND NOT AMEBAD_ENABLE_NIMBLE)
    set(_hash_grp ",mbedcrypto")
endif()

# iperf (make IPERF=ON): the four homegrown iperf2-protocol throughput probes
# (TCP/UDP x client/server) that auto-run from the network-up hook. They talk
# FreeRTOS-Plus-TCP sockets directly (FreeRTOS_socket/send) with a static TX buffer
# — no BSD-sockets/pthread/cJSON/mmap shim, no dynamic per-stream buffers — so they
# avoid the upstream-iperf3 port's heap churn. (iperf3 was tried as component/
# iperf3_config but its mmap/pthread/cJSON path corrupted the TLSF heap on TX.)
option(AMEBAD_ENABLE_IPERF "Build the homegrown iperf2 throughput probes" OFF)
if(AMEBAD_ENABLE_IPERF)
    # The four probes moved to the shared component/iperf_config (target iperf); main.c still
    # auto-runs iperf_server_start() from the network-up hook (AT_PROJECT_IPERF).
    target_link_libraries(target_img2_km4 PRIVATE iperf)
    set_property(SOURCE "${AT_MAIN_SRC}"
                 APPEND PROPERTY COMPILE_DEFINITIONS AT_PROJECT_IPERF)
endif()

# c-ares DNS resolver (CARES=ON): the AWS TLS/MQTT (tcp_sockets_wrapper.c, in
# network_transport_sockets), SNTP, and OTA HTTP paths resolve hostnames via c-ares instead of
# FreeRTOS+TCP's built-in resolver. cares_port pulls the c_ares archive; AT_PROJECT_CARES switches
# each call site (the FreeRTOS+TCP path stays as the CARES=OFF fallback). main.c (re)inits the
# channel + DNS servers on each network-up.
option(AMEBAD_ENABLE_CARES "Resolve DNS via c-ares (select event loop) instead of FreeRTOS+TCP" OFF)
if(AMEBAD_ENABLE_CARES)
    target_link_libraries(target_img2_km4 PRIVATE cares_port)
    target_compile_definitions(network_transport_sockets PRIVATE AT_PROJECT_CARES)
    target_link_libraries(network_transport_sockets PRIVATE cares_port)   # for cares_resolve.h
    set_property(SOURCE
                 "${CMAKE_CURRENT_SOURCE_DIR}/sntp_time.c"
                 "${CMAKE_CURRENT_SOURCE_DIR}/ota_app.c"
                 "${CMAKE_CURRENT_SOURCE_DIR}/ota_http.c"
                 "${AT_MAIN_SRC}"
                 "${CMAKE_CURRENT_SOURCE_DIR}/wifi_cli.c"
                 APPEND PROPERTY COMPILE_DEFINITIONS AT_PROJECT_CARES)
endif()

# Archives wrapped in --start-group (cross_refs) for circular vendor refs.
# libc (-lc) supplies __libc_init_array (KM4 has C++ global ctors; KM0 didn't)
# plus memcpy/memset — so KM4 doesn't need the ROM memcpy alias KM0 uses.
target_link_libraries(target_img2_km4 PRIVATE
    amebad_sdk_includes
    syscall_config       # OBJECT lib: cross-platform newlib retarget (VFS + malloc + byte-order), force-included so its strong defs beat libc's ENOSYS stubs
    amebad_syscall_port  # OBJECT lib: AmebaD console backend (strong at_console_default_backend -> LOGUART), also force-included
    amebad_mbedtls_rom   # INTERFACE: ROM mbedTLS 2.4.0 headers + MBEDTLS_CONFIG_FILE (no archive)
    # LittleFS (lfs_port + lfs_config + littlefs) and its flash backend
    # (amebad_mbed) join the group: littlefs is a leaf needing libc (printf/strspn)
    # + libgcc (__popcountsi2), which follow the group, and as a transitive
    # EXCLUDE_FROM_ALL lib CMake would otherwise sink it past -lc/-lgcc and leave
    # those undefined. amebad_mbed (flash_api) is lfs_port's backend. (The upstream
    # mbedcrypto/mbedx509 + picotls/ngtcp2 crypto join per-feature via the _grp vars.)
    "$<LINK_GROUP:cross_refs,FreeRTOS,amebad_osdep,amebad_ram_hp,amebad_ram_common,amebad_usrcfg_common,amebad_usrcfg_hp,amebad_pmc_hp,amebad_wifi,amebad_wlan_glue,amebad_freertos_tcp,freertos_plus_tcp,freertos_plus_tcp_port,amebad_wlan,amebad_wps,lfs_port,amebad_mbed,lfs_config,littlefs,rxi_log${_diag_grp}${_hci_grp}${_quic_grp}${_pk11_grp}${_bench_grp}${_aws_grp}${_mz_grp}${_hash_grp}>"
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
    # Resolve memcpy to the ROM _memcpy (0x10110d2d, string_rom_km4.ld) instead of newlib's XIP
    # copy. The console RX ISR's StreamBuffer path (RAM-resident, rlx8721d_img2_is.ld) calls memcpy
    # and can fire while the SPE programs flash (XIP unavailable); the ROM copy is always reachable.
    # A strong --defsym also stops newlib's memcpy from being pulled (image shrinks). Matches the
    # KM0 romalias (memcpy=_memcpy).
    "-Wl,--defsym=memcpy=_memcpy"
)

# Secure-world (TF-M): when AMEBAD_TFM_BOOT is on, app_km4 IS TF-M's Non-Secure
# side. It consumes TF-M's exported NS interface (built by app_km4_secure's
# tfm_spe ExternalProject) BY PATH — a single km4 build tree cannot
# add_subdirectory the export, which only exists after tfm_spe builds. So: put its
# headers on the include path, link s_veneers.o, and depend on tfm_spe so it
# builds + installs first. tfm_ns_check.c is a minimal compile/link probe; real
# PSA calls + NS layout/boot handoff are a follow-on stage.
if(AMEBAD_TFM_BOOT)
    # Pull the (non-standalone) TF-M SPE sibling into THIS app's build tree — it defines the tfm_spe
    # ExternalProject target + installs the NS interface export consumed below.
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../app_km4_secure ${CMAKE_BINARY_DIR}/app_km4_secure)
    set(_tfm_export "${CMAKE_BINARY_DIR}/app_km4_secure/tfm_spe_export")
    set(_tfm_src    "${AT_ROOT}/component/trusted-firmware-m_config/trusted-firmware-m")
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
    # (0x10070000..0x1007FFFF); app_km4 as the NSPE must keep its heap (and
    # everything else) under that or ram_heap_init faults the moment the heap touches
    # the secure region. __tz_sram_top__ feeds rlx8721d_img2_is.ld's __sram_end__
    # (= __HeapLimit) and MUST equal region_defs.h S_DATA_START. This is the heap-hang fix:
    # previously the NS heap ran up to BD_RAM_NS top 0x1007C000, overlapping the secure
    # window. 2026-06-24: raised 0x10060000 -> 0x10070000 (TF-M S_DATA 120 KB -> 56 KB),
    # +64 KB of NS heap.
    target_link_options(target_img2_km4 PRIVATE
        "-Wl,--defsym=__tz_sram_top__=0x10070000")

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
        set_property(SOURCE "${AT_MAIN_SRC}"
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
