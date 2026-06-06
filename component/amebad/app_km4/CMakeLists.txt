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
    "${CMAKE_CURRENT_SOURCE_DIR}/iperf_client.c"        # iperf2 TCP client (TX throughput to WSL iperf -s)
    "${CMAKE_CURRENT_SOURCE_DIR}/iperf_server.c"        # iperf2 TCP sink (RX throughput from WSL iperf -c)
    "${CMAKE_CURRENT_SOURCE_DIR}/iperf_udp_client.c"    # iperf2 UDP blast (bottleneck discriminator: driver vs TCP pacing)
    "${CMAKE_CURRENT_SOURCE_DIR}/iperf_udp_server.c"    # UDP RX sink (discriminator: is the RX/ACK delivery path tick-quantized?)
    "${CMAKE_CURRENT_SOURCE_DIR}/rom_mbedtls_kat.c"     # known-answer test of the ROM mbedTLS 2.4.0 software crypto
    "${CMAKE_SOURCE_DIR}/component/amebad/mbedtls_rom/rom_mbedtls.c"  # ROM mbedTLS ram-map glue (use_hw_crypto_func=0)
    "${CMAKE_CURRENT_SOURCE_DIR}/wolfcrypt_bench.c"     # on-target wolfCrypt v5.9.1 microbenchmark (SP/Thumb2 asm)
    "${CMAKE_CURRENT_SOURCE_DIR}/rom_hwcrypto_test.c"   # HW-vs-SW compare of rom_ssl_ram_map.h hw_crypto_* (IPSEC engine)
)
# WIP: the image2 link still needs the app/WiFi/mbedTLS/lwIP/log/os_dep/ftl
# stack (~30 undefined symbols at bootstrap). EXCLUDE_FROM_ALL keeps the
# default KM4 build (bootloader_km4 + the driver libs) green; build this
# target explicitly to continue closing the link:
#   cmake --build <dir> -t target_img2_km4
set_target_properties(target_img2_km4 PROPERTIES EXCLUDE_FROM_ALL TRUE)
set_property(TARGET target_img2_km4 APPEND PROPERTY LINK_DEPENDS "${_combined_ld}")
add_dependencies(target_img2_km4 amebad_km4_img2_ld)

# main.c #includes ftl_int.h (FTL) — vendored alongside ftl.c in ftl/.
target_include_directories(target_img2_km4 SYSTEM PRIVATE
    "${CMAKE_SOURCE_DIR}/component/amebad/ftl")

# BLE controller HCI test image: link the controller bring-up + HCI transport
# (amebad_hci, no btgap.a) and auto-enter the DTM/HCI bridge at boot. OFF by default.
option(AMEBAD_ENABLE_HCI "Build the BLE controller HCI test image (bring-up + HCI bridge)" OFF)
set(_hci_grp "")
if(AMEBAD_ENABLE_HCI)
    # btgap.a (Realtek upper stack) not used — controller bring-up runs via
    # hci_tp_open() directly (component/amebad/hci/hci_nostack.c). Just amebad_hci.
    set(_hci_grp ",amebad_hci")
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/main.c"
                 APPEND PROPERTY COMPILE_DEFINITIONS AT_PROJECT_HCI)
    # The controller firmware blob (normal BLE-DTM vs MP/RF-test, selected by
    # AMEBAD_HCI_FW_MP). In the EXECUTABLE (not amebad_hci) so its STRONG
    # rtlbt_(mp_)fw / _len always link and override the weak {0} stubs in
    # hci_fw_stub.c — a strong blob inside the archive would not get pulled.
    set(_hci_root "${CMAKE_SOURCE_DIR}/component/amebad/hci")
    if(AMEBAD_HCI_FW_MP)
        set(_hci_fw_src "${_hci_root}/hci_mp_patch.c")
    else()
        set(_hci_fw_src "${_hci_root}/hci_normal_patch.c")
    endif()
    set_source_files_properties("${_hci_fw_src}" PROPERTIES
        COMPILE_OPTIONS "-Wno-unused-variable;-Wno-unused-const-variable")
    # newlib syscall stubs (_write/_sbrk/...) for the HCI source's _rtl_printf/abort
    # — in the EXECUTABLE so they resolve libc.a's references (lib members to the
    # left of libc don't get pulled for right-side refs).
    target_sources(target_img2_km4 PRIVATE
        "${_hci_root}/hci_syscalls.c"
        "${_hci_fw_src}")
endif()

# Archives wrapped in --start-group (cross_refs) for circular vendor refs.
# libc (-lc) supplies __libc_init_array (KM4 has C++ global ctors; KM0 didn't)
# plus memcpy/memset — so KM4 doesn't need the ROM memcpy alias KM0 uses.
target_link_libraries(target_img2_km4 PRIVATE
    amebad_sdk_includes
    amebad_mbedtls_rom   # INTERFACE: ROM mbedTLS 2.4.0 headers + MBEDTLS_CONFIG_FILE (no archive)
    amebad_wolfssl       # wolfCrypt v5.9.1 (benchmark); before the group so its pvPortMalloc/DiagPrintf refs resolve
    "$<LINK_GROUP:cross_refs,FreeRTOS,amebad_freertos_glue_km4,amebad_osdep,amebad_ram_hp,amebad_ram_common,amebad_ftl,amebad_at_cmd,amebad_usrcfg_common,amebad_usrcfg_hp,amebad_monitor,amebad_monitor_common,amebad_pmc_hp,amebad_wifi,amebad_wlan_glue,amebad_freertos_tcp,amebad_wlan,amebad_wps${_hci_grp}>"
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
