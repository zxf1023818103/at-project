# AmebaD KM0 LP 2nd-stage bootloader (optional) — produces
# target_loader_2nd.axf / km0_boot_all_2nd.bin.
#
# Background: KM0's primary bootloader (component/amebad/bootloader_km0/)
# runs in KM0 BD_RAM (48K). For more complex boot logic (network OTA,
# secure boot extensions, ...) Realtek's SDK supports a 2nd-stage
# bootloader that runs in KM4 SRAM (BOOTLOADER2_RAM = BD_RAM_NS, 456K).
# See SDK readme.txt mirrored alongside this CMakeLists.txt.
#
# SDK default does NOT build this — neither does our CMake by default.
# Opt in with:
#   cmake ... -DAMEBAD_ENABLE_2ND_BOOT=ON
#
# Caveat: a runnable 2nd-boot chain also requires patching the primary
# bootloader (boot_flash_lp.c, boot_flash_hp.c, ram_lp/rtl8721dlp_km4.c)
# and relocating OTA region (default 0x08006000 collides with the
# 2nd-bootloader flash partition KM0_BOOT2; relocate via
# -DAMEBAD_OTA1_ADDR=...). See readme.txt steps 4-7. Those patches are
# NOT applied here — this component only ports the 2nd-stage bootloader
# build artifact. Wiring it into a real boot chain is future work.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    return()
endif()

option(AMEBAD_ENABLE_2ND_BOOT
    "Build KM0 2nd-stage bootloader (target_loader_2nd.axf)" OFF)
if(NOT AMEBAD_ENABLE_2ND_BOOT)
    return()
endif()

set(_src_dir       "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(_local_ld_dir  "${CMAKE_CURRENT_SOURCE_DIR}/ld")
# layout.ld + rom_symbol_acut.ld are shared with stage 1 (see
# CMAKE_PATCH notes in bootloader_km0/ld/rlx8721d_layout.ld for the
# BOOTLOADER2_RAM / KM0_BOOT2 segments).
set(_stage1_ld_dir "${CMAKE_SOURCE_DIR}/component/amebad/bootloader_km0/ld")

foreach(_p
        "${_src_dir}/boot_ram_lp_2nd.c"
        "${_local_ld_dir}/rlx8721d_boot_2nd.ld"
        "${_stage1_ld_dir}/rlx8721d_layout.ld"
        "${_stage1_ld_dir}/rlx8721d_rom_symbol_acut.ld")
    if(NOT EXISTS "${_p}")
        message(FATAL_ERROR "amebad/bootloader_km0_2nd: missing '${_p}'")
    endif()
endforeach()

set(_loader_srcs
    "${_src_dir}/boot_ram_lp_2nd.c"     # SDK: bootloader_2nd/boot_ram_lp_2nd.c
    "${_src_dir}/boot_flash_lp_2nd.c"   # SDK: bootloader_2nd/boot_flash_lp_2nd.c
)

# Linker script: cat boot_2nd.ld (local) + rom_symbol_acut.ld (stage 1)
# — layout.ld is INCLUDEd from boot_2nd.ld and resolved through the
# -L pointing at stage 1's ld dir.
set(_boot_ld       "${_local_ld_dir}/rlx8721d_boot_2nd.ld")
set(_rom_symbol_ld "${_stage1_ld_dir}/rlx8721d_rom_symbol_acut.ld")
set(_layout_ld     "${_stage1_ld_dir}/rlx8721d_layout.ld")
set(_combined_ld   "${CMAKE_CURRENT_BINARY_DIR}/rlx8721d.ld")

add_custom_command(
    OUTPUT  "${_combined_ld}"
    COMMAND "${CMAKE_COMMAND}" -E cat "${_boot_ld}" "${_rom_symbol_ld}"
                                       > "${_combined_ld}"
    DEPENDS "${_boot_ld}" "${_rom_symbol_ld}" "${_layout_ld}"
    COMMENT "concat KM0 2nd-stage loader linker script"
    VERBATIM
)
add_custom_target(amebad_km0_loader_2nd_ld DEPENDS "${_combined_ld}")

add_executable(target_loader_2nd ${_loader_srcs})
set_target_properties(target_loader_2nd PROPERTIES
    SUFFIX      ".axf"
    OUTPUT_NAME "target_loader_2nd"
)
set_property(TARGET target_loader_2nd APPEND PROPERTY LINK_DEPENDS "${_combined_ld}")
add_dependencies(target_loader_2nd amebad_km0_loader_2nd_ld)

# Same link list as bootloader_km0 — 2nd stage uses the same KM0 HAL
# drivers (ram_common + ram_lp) + board config (usrcfg_common/lp).
target_link_libraries(target_loader_2nd PRIVATE
    amebad_sdk_includes
    amebad_ram_common
    amebad_ram_lp
    amebad_usrcfg_common
    amebad_usrcfg_lp
)

target_link_options(target_loader_2nd PRIVATE
    "-T${_combined_ld}"
    "-L${_local_ld_dir}"
    "-L${_stage1_ld_dir}"
    "-Wl,--no-enum-size-warning"
    "-Wl,--warn-common"
    "-Wl,-Map=$<TARGET_FILE_DIR:target_loader_2nd>/target_loader_2nd.map"
)

target_compile_options(target_loader_2nd PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)

add_custom_command(TARGET target_loader_2nd POST_BUILD
    COMMAND "${CMAKE_SIZE}" -A --radix=16 "$<TARGET_FILE:target_loader_2nd>"
    COMMAND "${CMAKE_SIZE}" -t --radix=16 "$<TARGET_FILE:target_loader_2nd>"
    COMMENT "section sizes for target_loader_2nd.axf"
    VERBATIM
)
