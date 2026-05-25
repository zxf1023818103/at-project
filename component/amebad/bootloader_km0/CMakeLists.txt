# AmebaD KM0 LP bootloader — produces target_loader.axf.
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
# Sources (src/) and linker scripts (ld/) are project-local copies of the
# SDK files — see Stage 1.5 in the plan. Resync against the SDK when the
# platform/ameba-rtos-d submodule pointer moves:
#   diff -q component/amebad/bootloader_km0/src/boot_ram_lp.c \
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

# Project-local copies of SDK sources / linker scripts.
set(_src_dir       "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(_local_ld_dir  "${CMAKE_CURRENT_SOURCE_DIR}/ld")

# Sanity check — friendlier than a downstream "file not found" from gcc/ld.
foreach(_p
        "${_src_dir}/boot_ram_lp.c"
        "${_local_ld_dir}/rlx8721d_boot.ld"
        "${_local_ld_dir}/rlx8721d_layout.ld")
    if(NOT EXISTS "${_p}")
        message(FATAL_ERROR "amebad/bootloader_km0: missing '${_p}'")
    endif()
endforeach()

set(_loader_srcs
    "${_src_dir}/boot_ram_lp.c"             # SDK: bootloader/boot_ram_lp.c
    "${_src_dir}/boot_flash_lp.c"           # SDK: bootloader/boot_flash_lp.c
)

# Linker script: cat boot.ld + rom_symbol_acut.ld into build dir
# (mirrors copy_ld_img1 + rom-symbol append in Makefile.include.gen).
set(_boot_ld       "${_local_ld_dir}/rlx8721d_boot.ld")
set(_rom_symbol_ld "${_local_ld_dir}/rlx8721d_rom_symbol_acut.ld")
set(_layout_ld     "${_local_ld_dir}/rlx8721d_layout.ld")
set(_combined_ld   "${CMAKE_CURRENT_BINARY_DIR}/rlx8721d.ld")

add_custom_command(
    OUTPUT  "${_combined_ld}"
    COMMAND "${CMAKE_COMMAND}" -E cat "${_boot_ld}" "${_rom_symbol_ld}"
                                       > "${_combined_ld}"
    DEPENDS "${_boot_ld}" "${_rom_symbol_ld}" "${_layout_ld}"
    COMMENT "concat KM0 loader linker script"
    VERBATIM
)
add_custom_target(amebad_km0_loader_ld DEPENDS "${_combined_ld}")

add_executable(target_loader ${_loader_srcs})
set_target_properties(target_loader PROPERTIES
    SUFFIX      ".axf"
    OUTPUT_NAME "target_loader"
)
set_property(TARGET target_loader APPEND PROPERTY LINK_DEPENDS "${_combined_ld}")
add_dependencies(target_loader amebad_km0_loader_ld)

# Vendor includes + HAL libs + board config. amebad_sdk_includes brings
# in the 30+ SDK header paths + amebad_fwlib_headers transitively;
# ram_common / ram_lp provide HAL drivers (e.g. WDG_Cmd from
# ram_common/rtl8721d_wdg.c); usrcfg_common / usrcfg_lp provide board
# config (boot config, flash config, pinmap etc.).
target_link_libraries(target_loader PRIVATE
    amebad_sdk_includes
    amebad_ram_common
    amebad_ram_lp
    amebad_usrcfg_common
    amebad_usrcfg_lp
)

# Per-target link delta over what arm-none-eabi.cmake already provides:
#   -T <combined.ld>             linker script
#   -L <ld>/                     so INCLUDE "rlx8721d_layout.ld" resolves
#   -Wl,--no-enum-size-warning   from LD_ARG line 312
#   -Wl,--warn-common            from LD_ARG line 313
#   -Wl,-Map=<bin>/target_loader.map
target_link_options(target_loader PRIVATE
    "-T${_combined_ld}"
    "-L${_local_ld_dir}"
    "-Wl,--no-enum-size-warning"
    "-Wl,--warn-common"
    "-Wl,-Map=$<TARGET_FILE_DIR:target_loader>/target_loader.map"
)

# Vendor code — suppress -Wall/-Wextra noise we don't own.
target_compile_options(target_loader PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)

# Post-build size dump (mirrors SDK CC_SIZE on linker_loader rule).
add_custom_command(TARGET target_loader POST_BUILD
    COMMAND "${CMAKE_SIZE}" -A --radix=16 "$<TARGET_FILE:target_loader>"
    COMMAND "${CMAKE_SIZE}" -t --radix=16 "$<TARGET_FILE:target_loader>"
    COMMENT "section sizes for target_loader.axf"
    VERBATIM
)
