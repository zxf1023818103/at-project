# AmebaD KM0 LP bootloader — produces the target_loader_km0 ELF.
#
# Stage 1 of the KM0 image pipeline: ELF/AXF link only. Stage 2
# (objcopy / prepend / cat / imagetool.sh -> km0_boot_all.bin) is a
# separate POST_BUILD chain to be added once this stage is verified.
#
# Mirrors the linker_loader rule in
#   platform/ameba-rtos-d/.../project_lp/asdk/Makefile
# and the LD_ARG block in
#   platform/ameba-rtos-d/.../project_lp/asdk/Makefile.include.gen (lines 293-313).
#
# Sources and linker scripts are project-local copies of the SDK files (flat in
# this folder) — see Stage 1.5 in the plan. Resync against the SDK when the
# platform/ameba-rtos-d submodule pointer moves:
#   diff -q component/amebad/bootloader_km0/boot_ram_lp.c \
#           platform/ameba-rtos-d/component/soc/realtek/amebad/bootloader/boot_ram_lp.c
#
# Headers + IFLAGS come transitively through amebad_sdk_includes.
# HAL drivers come from amebad_ram_common + amebad_ram_lp static libs.
# Board configuration (bootcfg etc.) comes from amebad_usrcfg_common +
# amebad_usrcfg_lp. Only the 2 .c specific to KM0 LP boot stay here:
# bootloader/boot_ram_lp.c, bootloader/boot_flash_lp.c.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    message(FATAL_ERROR
        "amebad/bootloader_km0: requires "
        "-DCMAKE_TOOLCHAIN_FILE=cmake/amebad-km0.cmake "
        "(AMEBAD_CHIP='${AMEBAD_CHIP}', expected 'amebad-km0')")
endif()

# Project-local copies of SDK sources / linker scripts (flat in this folder).

# Sanity check — friendlier than a downstream "file not found" from gcc/ld.
foreach(_p
        "${CMAKE_CURRENT_SOURCE_DIR}/boot_ram_lp.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/rlx8721d_boot.ld"
        "${AMEBAD_COMMON_LAYOUT_LD}")
    if(NOT EXISTS "${_p}")
        message(FATAL_ERROR "amebad/bootloader_km0: missing '${_p}'")
    endif()
endforeach()

set(_loader_srcs
    "${CMAKE_CURRENT_SOURCE_DIR}/boot_ram_lp.c"             # SDK: bootloader/boot_ram_lp.c
    "${CMAKE_CURRENT_SOURCE_DIR}/boot_flash_lp.c"           # SDK: bootloader/boot_flash_lp.c
)

# Linker script: cat boot.ld + rom_symbol_acut.ld into build dir
# (mirrors copy_ld_img1 + rom-symbol append in Makefile.include.gen).
# layout.ld + rom_symbol_acut.ld are the KM0-image fragments shared via
# component/amebad/common_ld_script/ (AMEBAD_COMMON_*_LD); only boot.ld
# stays local to this stage.
set(_boot_ld       "${CMAKE_CURRENT_SOURCE_DIR}/rlx8721d_boot.ld")
set(_rom_symbol_ld "${AMEBAD_COMMON_ROM_SYMBOL_LD}")
set(_layout_ld     "${AMEBAD_COMMON_LAYOUT_LD}")
set(_combined_ld   "${CMAKE_CURRENT_BINARY_DIR}/rlx8721d.ld")

add_custom_command(
    OUTPUT  "${_combined_ld}"
    COMMAND "${CMAKE_COMMAND}" -E cat "${_boot_ld}" "${_rom_symbol_ld}"
                                       "${AMEBAD_FWLIB_ROM_KM0_LD}" "${AMEBAD_STRING_ROM_KM0_LD}" "${AMEBAD_CMSIS_ROM_KM0_LD}" "${AMEBAD_MONITOR_ROM_LD}" "${AMEBAD_XMODEM_ROM_SYMBOL_LD}" > "${_combined_ld}"
    DEPENDS "${_boot_ld}" "${_rom_symbol_ld}" "${AMEBAD_FWLIB_ROM_KM0_LD}" "${AMEBAD_STRING_ROM_KM0_LD}" "${AMEBAD_CMSIS_ROM_KM0_LD}" "${AMEBAD_MONITOR_ROM_LD}" "${AMEBAD_XMODEM_ROM_SYMBOL_LD}" "${_layout_ld}"
    COMMENT "concat KM0 loader linker script"
    VERBATIM
)
add_custom_target(amebad_km0_loader_ld DEPENDS "${_combined_ld}")

# CMake target is suffixed with _km0 (vs. _km4 for the HP bootloader and
# _2nd for the KM0 2nd-stage loader) — bootloader_km{0,4}/ are added under
# separate AMEBAD_CHIP-gated branches, so they never share a CMake run today,
# but a chip-agnostic "target_loader" name would still collide if a future
# build wants to configure both at once. The on-disk filename follows the
# target name (no SUFFIX/OUTPUT_NAME override) — the eventual
# kmX_boot_all.bin produced by image_tool/ is what matters downstream, the
# intermediate ELF name is informational.
add_executable(target_loader_km0 ${_loader_srcs})
set_property(TARGET target_loader_km0 APPEND PROPERTY LINK_DEPENDS "${_combined_ld}")
add_dependencies(target_loader_km0 amebad_km0_loader_ld)

# Vendor includes + HAL libs + board config. amebad_sdk_includes brings
# in the 30+ SDK header paths + amebad_fwlib_headers transitively;
# ram_common / ram_lp provide HAL drivers (e.g. WDG_Cmd from
# ram_common/rtl8721d_wdg.c); usrcfg_common / usrcfg_lp provide board
# config (boot config, flash config, pinmap etc.).
target_link_libraries(target_loader_km0 PRIVATE
    amebad_sdk_includes
    amebad_common_ld_script
    amebad_ram_common
    amebad_ram_lp
    amebad_usrcfg_common
    amebad_usrcfg_lp
)

# Per-target link delta over what arm-none-eabi.cmake already provides:
#   -T <combined.ld>             linker script
#   -Wl,--no-enum-size-warning   from LD_ARG line 312
#   -Wl,--warn-common            from LD_ARG line 313
#   -Wl,-Map=<bin>/target_loader.map
# The -L for INCLUDE "rlx8721d_layout.ld" comes from amebad_common_ld_script
# (target_link_directories). boot.ld is -T'd and rom_symbol_acut.ld is cat'd
# by absolute path, so this stage's local ld/ no longer needs to be on -L.
target_link_options(target_loader_km0 PRIVATE
    "-T${_combined_ld}"
    "-Wl,--no-enum-size-warning"
    "-Wl,--warn-common"
    "-Wl,-Map=$<TARGET_FILE_DIR:target_loader_km0>/target_loader.map"
)

# Vendor code — suppress -Wall/-Wextra noise we don't own.
target_compile_options(target_loader_km0 PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)

# Post-build size dump (mirrors SDK CC_SIZE on linker_loader rule).
add_custom_command(TARGET target_loader_km0 POST_BUILD
    COMMAND "${CMAKE_SIZE}" -A --radix=16 "$<TARGET_FILE:target_loader_km0>"
    COMMAND "${CMAKE_SIZE}" -t --radix=16 "$<TARGET_FILE:target_loader_km0>"
    COMMENT "section sizes for target_loader_km0"
    VERBATIM
)

# Stage 2: split ELF -> ram_1.bin / xip_boot.bin, prepend 32-byte headers,
# cat -> km0_boot_all.bin, optional RSIP encrypt @ 0x08000000. Helper is
# defined in component/amebad/image_tool/CMakeLists.txt.
amebad_make_boot_all_bin(target_loader_km0 km0_boot_all.bin 0x08000000)
