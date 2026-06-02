# AmebaD KM0 LP application image (image2) — produces target_img2_km0.axf.
#
# Stage 1 of the KM0 image2 pipeline: ELF/AXF link. Mirrors the
# linker_image2 rule in
#   platform/ameba-rtos-d/.../project_lp/asdk/Makefile
# RAM_OBJS  -> our CMake libs (freertos / ram_* / usrcfg_* / monitor + main.c)
# ROM       -> rlx8721d_rom_symbol_acut.ld PROVIDE table (CONFIG_LINK_ROM_SYMB=1,
#              so no rom.a — same approach as the bootloaders)
# APP_LIB   -> prebuilt lib_pmc_lp.a (PMC/SOCPS deep-sleep) + lib_wifi_fw.a
#              (wifi_FW_init_ram; CONFIG_WIFI_FW_EN=1) + libm
#
# main.c + ld/rlx8721d_img2.ld are project-local copies of the SDK
# (src_lp/main.c, ld/rlx8721d_img2.ld). rlx8721d_layout.ld + the ROM symbol
# table are shared via component/amebad/common_ld_script.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    return()
endif()

set(_src_dir      "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(_local_ld_dir "${CMAKE_CURRENT_SOURCE_DIR}/ld")

foreach(_p
        "${_src_dir}/main.c"
        "${_local_ld_dir}/rlx8721d_img2.ld"
        "${AMEBAD_COMMON_LAYOUT_LD}"
        "${AMEBAD_COMMON_ROM_SYMBOL_LD}")
    if(NOT EXISTS "${_p}")
        message(FATAL_ERROR "amebad/app_km0: missing '${_p}'")
    endif()
endforeach()

# Prebuilt vendor archives (tracked in the ameba-rtos-d submodule). Referenced
# in-place as IMPORTED libs — they're binary blobs with no source to compile.
set(_lp_lib "${CMAKE_SOURCE_DIR}/platform/ameba-rtos-d/project/realtek_amebaD_va0_example/GCC-RELEASE/project_lp/asdk/lib/application")
add_library(amebad_pmc_lp STATIC IMPORTED)
set_target_properties(amebad_pmc_lp PROPERTIES
    IMPORTED_LOCATION "${_lp_lib}/lib_pmc_lp.a")
add_library(amebad_wifi_fw STATIC IMPORTED)
set_target_properties(amebad_wifi_fw PROPERTIES
    IMPORTED_LOCATION "${_lp_lib}/lib_wifi_fw.a")

# Linker script: cat img2.ld + rom_symbol_acut.ld into the build dir; img2.ld
# does INCLUDE "rlx8721d_layout.ld" (no path), resolved through the -L that
# amebad_common_ld_script puts on the link line.
set(_img2_ld     "${_local_ld_dir}/rlx8721d_img2.ld")
set(_romalias_ld "${_local_ld_dir}/rlx8721d_img2_romalias.ld")
set(_combined_ld "${CMAKE_CURRENT_BINARY_DIR}/rlx8721d.ld")
# img2.ld + ROM symbol table + memcpy/memset->ROM aliases (alias fragment last,
# so _memcpy/_memset are defined by the time its PROVIDEs evaluate).
add_custom_command(
    OUTPUT  "${_combined_ld}"
    COMMAND "${CMAKE_COMMAND}" -E cat "${_img2_ld}" "${AMEBAD_COMMON_ROM_SYMBOL_LD}"
                                       "${_romalias_ld}" > "${_combined_ld}"
    DEPENDS "${_img2_ld}" "${AMEBAD_COMMON_ROM_SYMBOL_LD}" "${AMEBAD_COMMON_LAYOUT_LD}"
            "${_romalias_ld}"
    COMMENT "concat KM0 image2 linker script"
    VERBATIM
)
add_custom_target(amebad_km0_img2_ld DEPENDS "${_combined_ld}")

add_executable(target_img2_km0 "${_src_dir}/main.c")
set_property(TARGET target_img2_km0 APPEND PROPERTY LINK_DEPENDS "${_combined_ld}")
add_dependencies(target_img2_km0 amebad_km0_img2_ld)

# Archives wrapped in a --start-group/--end-group (RESCAN): the prebuilt
# vendor libs (pmc / wifi_fw) and our HAL/FreeRTOS libs have circular symbol
# references that a single left-to-right pass won't resolve. INTERFACE libs
# (sdk_includes / common_ld_script) stay outside the group.
target_link_libraries(target_img2_km0 PRIVATE
    amebad_sdk_includes
    amebad_common_ld_script
    "$<LINK_GROUP:cross_refs,amebad_freertos_km0,amebad_ram_common,amebad_ram_lp,amebad_usrcfg_common,amebad_usrcfg_lp,amebad_monitor_km0,amebad_pmc_lp,amebad_wifi_fw>"
    m
)

# Per-target link delta:
#   -T <combined.ld>             linker script (img2 + rom symbols)
#   -Wl,--no-enum-size-warning / --warn-common   parity with bootloaders
#   -Wl,-Map=<bin>/target_img2.map
# -L for INCLUDE "rlx8721d_layout.ld" comes from amebad_common_ld_script.
target_link_options(target_img2_km0 PRIVATE
    "-T${_combined_ld}"
    "-Wl,--no-enum-size-warning"
    "-Wl,--warn-common"
    "-Wl,-Map=$<TARGET_FILE_DIR:target_img2_km0>/target_img2.map"
)

# Vendor code — suppress -Wall/-Wextra noise we don't own.
target_compile_options(target_img2_km0 PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)

# Post-build size dump (mirrors SDK CC_SIZE).
add_custom_command(TARGET target_img2_km0 POST_BUILD
    COMMAND "${CMAKE_SIZE}" -A --radix=16 "$<TARGET_FILE:target_img2_km0>"
    COMMAND "${CMAKE_SIZE}" -t --radix=16 "$<TARGET_FILE:target_img2_km0>"
    COMMENT "section sizes for target_img2_km0"
    VERBATIM
)

# Stage 2 (image2 -> km0_image2_all.bin: objcopy 3 segments, prepend IMG2SIGN
# headers, cat, 4K pad, optional RSIP @ 0x0c000000) is added next, via an
# image2 variant of the image_tool helper.
