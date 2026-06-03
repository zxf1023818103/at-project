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
# main.c + the ld/ scripts are project-local copies of the SDK
# (src_hp/main.c, project_hp ld/).

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

set(_src_dir      "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(_local_ld_dir "${CMAKE_CURRENT_SOURCE_DIR}/ld")

foreach(_p
        "${_src_dir}/main.c"
        "${_local_ld_dir}/rlx8721d_img2_is.ld"
        "${_local_ld_dir}/rlx8721d_layout_is.ld"
        "${_local_ld_dir}/rlx8721d_rom_symbol_acut.ld")
    if(NOT EXISTS "${_p}")
        message(FATAL_ERROR "amebad/app_km4: missing '${_p}'")
    endif()
endforeach()

# Prebuilt KM4 vendor archives (tracked in the ameba-rtos-d submodule).
set(_hp_lib "${CMAKE_SOURCE_DIR}/platform/ameba-rtos-d/project/realtek_amebaD_va0_example/GCC-RELEASE/project_hp/asdk/lib/application")
add_library(amebad_pmc_hp STATIC IMPORTED)
set_target_properties(amebad_pmc_hp PROPERTIES IMPORTED_LOCATION "${_hp_lib}/lib_pmc_hp.a")
# (lib_wlan.a / lib_wps.a intentionally not linked — this minimal image2 has no
# WiFi. They come back when the WiFi stack is wired.)

# Linker script: cat img2_is.ld + rom_symbol_acut.ld; img2_is.ld does
# INCLUDE "rlx8721d_layout_is.ld", resolved through -L below.
set(_img2_ld     "${_local_ld_dir}/rlx8721d_img2_is.ld")
set(_rom_sym_ld  "${_local_ld_dir}/rlx8721d_rom_symbol_acut.ld")
set(_layout_ld   "${_local_ld_dir}/rlx8721d_layout_is.ld")
set(_combined_ld "${CMAKE_CURRENT_BINARY_DIR}/rlx8721d.ld")
add_custom_command(
    OUTPUT  "${_combined_ld}"
    COMMAND "${CMAKE_COMMAND}" -E cat "${_img2_ld}" "${_rom_sym_ld}" > "${_combined_ld}"
    DEPENDS "${_img2_ld}" "${_rom_sym_ld}" "${_layout_ld}"
    COMMENT "concat KM4 image2 linker script"
    VERBATIM
)
add_custom_target(amebad_km4_img2_ld DEPENDS "${_combined_ld}")

add_executable(target_img2_km4
    "${_src_dir}/main.c"
    "${_src_dir}/cm_backtrace_stub.c"   # weak no-op (CmBacktrace lib not in this checkout)
    "${_src_dir}/minimal_stubs.c"       # weak CRYPTO_Init / rtw_if_wifi_thread (no TLS/WiFi)
)
# WIP: the image2 link still needs the app/WiFi/mbedTLS/lwIP/log/os_dep/ftl
# stack (~30 undefined symbols at bootstrap). EXCLUDE_FROM_ALL keeps the
# default KM4 build (bootloader_km4 + the driver libs) green; build this
# target explicitly to continue closing the link:
#   cmake --build <dir> -t target_img2_km4
set_target_properties(target_img2_km4 PROPERTIES EXCLUDE_FROM_ALL TRUE)
set_property(TARGET target_img2_km4 APPEND PROPERTY LINK_DEPENDS "${_combined_ld}")
add_dependencies(target_img2_km4 amebad_km4_img2_ld)

# main.c #includes ftl_int.h (FTL) — vendored alongside ftl.c in ftl_km4/src/.
target_include_directories(target_img2_km4 SYSTEM PRIVATE
    "${CMAKE_SOURCE_DIR}/component/amebad/ftl_km4/src")

# Archives wrapped in --start-group (cross_refs) for circular vendor refs.
# libc (-lc) supplies __libc_init_array (KM4 has C++ global ctors; KM0 didn't)
# plus memcpy/memset — so KM4 doesn't need the ROM memcpy alias KM0 uses.
target_link_libraries(target_img2_km4 PRIVATE
    amebad_sdk_includes
    "$<LINK_GROUP:cross_refs,FreeRTOS,amebad_freertos_glue_km4,amebad_osdep,amebad_ram_hp,amebad_ram_common,amebad_ftl_km4,amebad_at_cmd_km4,amebad_usrcfg_common,amebad_usrcfg_hp,amebad_monitor_km4,amebad_monitor_common,amebad_pmc_hp>"
    m
    stdc++
    c
)

target_link_options(target_img2_km4 PRIVATE
    "-T${_combined_ld}"
    "-L${_local_ld_dir}"
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
