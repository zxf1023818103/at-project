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
    "${CMAKE_CURRENT_SOURCE_DIR}/wolfcrypt_glue.c"      # wolfSSL XMALLOC/RNG glue (always, when wolfSSL is linked)
)

# Diagnostics (iperf throughput + ROM-mbedTLS / wolfCrypt benchmarks). There is
# no --gc-sections on this link, so unused TUs are only removed by excluding the
# source. The QUIC image drops them all (smaller KM4 image2 .text -> shifts the
# XIP boot path away from the layout-sensitive hang; see
# doc/amebad_xip_layout_boot_hang.md). main.c gates the matching calls under
# !AT_PROJECT_QUIC.
if(NOT AMEBAD_ENABLE_QUIC)
    target_sources(target_img2_km4 PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/iperf_client.c"        # iperf2 TCP client (TX throughput to WSL iperf -s)
        "${CMAKE_CURRENT_SOURCE_DIR}/iperf_server.c"        # iperf2 TCP sink (RX throughput from WSL iperf -c)
        "${CMAKE_CURRENT_SOURCE_DIR}/iperf_udp_client.c"    # iperf2 UDP blast (bottleneck discriminator: driver vs TCP pacing)
        "${CMAKE_CURRENT_SOURCE_DIR}/iperf_udp_server.c"    # UDP RX sink (discriminator: is the RX/ACK delivery path tick-quantized?)
        "${CMAKE_CURRENT_SOURCE_DIR}/rom_mbedtls_kat.c"     # known-answer test of the ROM mbedTLS 2.4.0 software crypto
        "${CMAKE_SOURCE_DIR}/component/amebad/mbedtls_rom/rom_mbedtls.c"  # ROM mbedTLS ram-map glue (use_hw_crypto_func=0)
        "${CMAKE_CURRENT_SOURCE_DIR}/wolfcrypt_bench.c"     # on-target wolfCrypt v5.9.1 microbenchmark (SP/Thumb2 asm)
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
#   AMEBAD_ENABLE_NIMBLE — Apache NimBLE host (LE/GATT/Mesh/coex), via component/nimble.
# Both ride the same controller bring-up (amebad_hci + firmware blob; the newlib
# syscalls come from amebad_syscalls, linked unconditionally below). NimBLE
# additionally links the `nimble` lib. Pick one. OFF by default.
option(AMEBAD_ENABLE_HCI    "Build the BLE controller HCI test image (bring-up + HCI bridge)" OFF)
option(AMEBAD_ENABLE_NIMBLE "Build the NimBLE host image (LE/GATT/Mesh/coex on amebad_hci)" OFF)
set(_hci_grp "")
if(AMEBAD_ENABLE_HCI OR AMEBAD_ENABLE_NIMBLE)
    # Shared controller bits — bring-up via hci_tp_open() (component/amebad/hci/
    # hci_nostack.c) + the firmware blob in the EXECUTABLE so its STRONG defs
    # override the weak {0} stubs (a blob inside the archive wouldn't get pulled).
    # newlib syscalls now come from amebad_syscalls (linked below).
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
    set(_hci_grp ",amebad_hci,nimble")
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/main.c"
                 APPEND PROPERTY COMPILE_DEFINITIONS AT_PROJECT_NIMBLE)
elseif(AMEBAD_ENABLE_HCI)
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/main.c"
                 APPEND PROPERTY COMPILE_DEFINITIONS AT_PROJECT_HCI)
endif()

# QUIC self-test (ngtcp2 + full wolfSSL TLS 1.3). The component/amebad/ngtcp2
# targets are only defined when AMEBAD_ENABLE_QUIC is ON (see component/amebad/
# CMakeLists.txt), which also forces AMEBAD_WOLFSSL_TLS so amebad_wolfssl (already
# in the link group) carries the TLS/QUIC layer. ngtcp2 + crypto join the group:
# crypto<->wolfssl<->ngtcp2 are mutually referenced.
# AT-command / monitor shell libs: linked by default, dropped from the QUIC
# image (shell init is gated off in main.c under AT_PROJECT_QUIC). Removing them
# changes the KM4 image2 XIP relative layout (boot-hang dodge) and shrinks it.
set(_diag_grp ",amebad_at_cmd,amebad_monitor,amebad_monitor_common")

set(_quic_grp "")
if(AMEBAD_ENABLE_QUIC)
    set(_diag_grp "")
    set(_quic_grp ",amebad_ngtcp2_crypto,amebad_ngtcp2")
    target_sources(target_img2_km4 PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/quic_test.c")   # newlib syscalls now from amebad_syscalls
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/main.c"
                 APPEND PROPERTY COMPILE_DEFINITIONS AT_PROJECT_QUIC)
endif()

# Archives wrapped in --start-group (cross_refs) for circular vendor refs.
# libc (-lc) supplies __libc_init_array (KM4 has C++ global ctors; KM0 didn't)
# plus memcpy/memset — so KM4 doesn't need the ROM memcpy alias KM0 uses.
target_link_libraries(target_img2_km4 PRIVATE
    amebad_sdk_includes
    amebad_syscalls      # OBJECT lib: newlib retarget (_write/_read/_sbrk/...) force-included so its strong defs beat libc's ENOSYS stubs
    amebad_mbedtls_rom   # INTERFACE: ROM mbedTLS 2.4.0 headers + MBEDTLS_CONFIG_FILE (no archive)
    # amebad_wolfssl is INSIDE the group: NimBLE's SM crypto (nimble, in the group)
    # pulls wc_* (wc_ecc_*/wc_AesEncryptDirect) that the bench didn't, so wolfCrypt
    # must rescan alongside nimble — wolfCrypt<->FreeRTOS<->nimble are mutually refed.
    "$<LINK_GROUP:cross_refs,FreeRTOS,amebad_freertos_glue_km4,amebad_osdep,amebad_ram_hp,amebad_ram_common,amebad_usrcfg_common,amebad_usrcfg_hp,amebad_pmc_hp,amebad_wifi,amebad_wlan_glue,amebad_freertos_tcp,amebad_wlan,amebad_wps,amebad_wolfssl${_diag_grp}${_hci_grp}${_quic_grp}>"
    m
    stdc++
    c
    gcc   # libgcc: __aeabi_uldivmod (64-bit divide in wolfSSL sp_int.c / bench reports)
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
    set(_tfm_src    "${CMAKE_SOURCE_DIR}/component/thirdparty/trusted-firmware-m")
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
        "${_tfm_src}/interface/src/os_wrapper/tfm_ns_interface_bare_metal.c")
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
endif()

# XIP boot-hang dodge: shift the whole .xip_image2.text block by AMEBAD_XIP_PAD
# bytes (consumed by rlx8721d_img2_is.ld via __xip_pad__) to move app_start's
# early fwlib callees off the layout-sensitive hanging address. Sweep this when
# the KM4 image hangs at boot (console stuck after the KM0 "RRAM:" line).
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
