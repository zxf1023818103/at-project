# ---- standalone CMake project (lib/executable split: libs in component, this image here) ----
# Built via `cmake -S project/amebad/bootloader_km4 -B build/bootloader_km4 -DCMAKE_TOOLCHAIN_FILE=
# cmake/amebad-km4.cmake` (the toolchain FORCE-sets AMEBAD_CHIP). AT_ROOT anchors the repo root.
cmake_minimum_required(VERSION 3.20)
project(bootloader_km4 C CXX ASM)
get_filename_component(AT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(AT_ROOT "${AT_ROOT}" CACHE INTERNAL "at-project repo root")
add_subdirectory(${AT_ROOT}/component ${CMAKE_BINARY_DIR}/component)
# This bootloader has no app of its own, but the lib-side amebad_sdk_includes links
# amebad_proj_config for the per-core platform_opts/autoconf headers fwlib needs — point it at
# the matching app_km4 config dir (what the old shared per-core configure provided).
add_library(amebad_proj_config INTERFACE)
target_include_directories(amebad_proj_config SYSTEM INTERFACE "${CMAKE_CURRENT_LIST_DIR}/../app_km4")

# AmebaD KM4 HP bootloader — produces the target_loader_km4 ELF.
#
# Stage 1 of the KM4 image pipeline: ELF/AXF link only. Mirrors
# bootloader_km0/ — same recipe, KM4-specific sources + ld scripts.
#
# Source-of-truth: project_hp/asdk/Makefile (linker_loader rule) +
# Makefile.include.gen (LD_ARG lines 463-487).
#
# Sources and linker scripts are project-local copies of the SDK (flat in this
# folder). Resync with the submodule when its pointer moves:
#   diff -q project/amebad/bootloader_km4/boot_ram_hp.c \
#           platform/ameba-rtos-d/component/soc/realtek/amebad/bootloader/boot_ram_hp.c
#
# Headers + IFLAGS via amebad_sdk_includes (KM4 branch selects app_km4
# project config + FreeRTOS RTL8721D_HP port). HAL drivers are pulled from on-chip
# ROM via rom_symbol_acut_boot.ld PROVIDE — KM4 bootloader links NO
# ram_* libs (SDK Makefile only edits drivers/ for KM4, never ram_common).
# Board config (bootcfg, trustzonecfg) comes via amebad_usrcfg_common
# + amebad_usrcfg_hp — usrcfg_common's chip-agnostic .c (bootcfg /
# ipccfg / wificfg) is built twice (once per chip toolchain), giving
# us a clean KM4 ABI .o without duplicating .c on disk.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    message(FATAL_ERROR
        "amebad/bootloader_km4: requires "
        "-DCMAKE_TOOLCHAIN_FILE=cmake/amebad-km4.cmake "
        "(AMEBAD_CHIP='${AMEBAD_CHIP}', expected 'amebad-km4')")
endif()

foreach(_p
        "${CMAKE_CURRENT_SOURCE_DIR}/boot_ram_hp.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/rlx8721d_img1_s.ld"
        "${CMAKE_CURRENT_SOURCE_DIR}/rlx8721d_layout_tz.ld")
    if(NOT EXISTS "${_p}")
        message(FATAL_ERROR "amebad/bootloader_km4: missing '${_p}'")
    endif()
endforeach()

set(_loader_srcs
    "${CMAKE_CURRENT_SOURCE_DIR}/boot_ram_hp.c"                  # SDK: bootloader/boot_ram_hp.c
    "${CMAKE_CURRENT_SOURCE_DIR}/boot_flash_hp.c"                # SDK: bootloader/boot_flash_hp.c
    "${CMAKE_CURRENT_SOURCE_DIR}/boot_trustzone_hp.c"            # SDK: bootloader/boot_trustzone_hp.c
)

# Linker script: cat img1_s.ld + rom_symbol_acut_boot.ld into build dir.
# img1_s.ld does INCLUDE "rlx8721d_layout_tz.ld" (no path), so -L below
# resolves it from this folder.
set(_boot_ld       "${CMAKE_CURRENT_SOURCE_DIR}/rlx8721d_img1_s.ld")
set(_rom_symbol_ld "${CMAKE_CURRENT_SOURCE_DIR}/rlx8721d_rom_symbol_acut_boot.ld")
set(_layout_ld     "${CMAKE_CURRENT_SOURCE_DIR}/rlx8721d_layout_tz.ld")
set(_combined_ld   "${CMAKE_CURRENT_BINARY_DIR}/rlx8721d.ld")

add_custom_command(
    OUTPUT  "${_combined_ld}"
    COMMAND "${CMAKE_COMMAND}" -E cat "${_boot_ld}" "${_rom_symbol_ld}"
                                       > "${_combined_ld}"
    DEPENDS "${_boot_ld}" "${_rom_symbol_ld}" "${_layout_ld}"
    COMMENT "concat KM4 loader linker script"
    VERBATIM
)
add_custom_target(amebad_km4_loader_ld DEPENDS "${_combined_ld}")

# Suffixed _km4 — see bootloader_km0/CMakeLists.txt for the rationale.
add_executable(target_loader_km4 ${_loader_srcs})
set_property(TARGET target_loader_km4 APPEND PROPERTY LINK_DEPENDS "${_combined_ld}")
add_dependencies(target_loader_km4 amebad_km4_loader_ld)

target_link_libraries(target_loader_km4 PRIVATE
    amebad_sdk_includes
    amebad_usrcfg_common
    amebad_usrcfg_hp
)

# Per-target link delta over arm-none-eabi.cmake + amebad-km4.cmake:
#   -T <combined.ld>             linker script
#   -L <ld>/                     INCLUDE rlx8721d_layout_tz.ld resolves
#   -Wl,--no-enum-size-warning   LD_ARG (KM4 doesn't have it explicitly,
#                                added for parity with KM0)
#   -Wl,--warn-common            same
#   -Wl,-Map=<bin>/target_loader.map
target_link_options(target_loader_km4 PRIVATE
    "-T${_combined_ld}"
    "-L${CMAKE_CURRENT_SOURCE_DIR}"
    "-Wl,--no-enum-size-warning"
    "-Wl,--warn-common"
    "-Wl,-Map=$<TARGET_FILE_DIR:target_loader_km4>/target_loader.map"
)

target_compile_options(target_loader_km4 PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)

# TF-M-first boot: divert the KM4 bootloader to tfm_s (copied from flash
# 0x08100000 to secure SRAM, secure-jump). See component/amebad/CMakeLists.txt.
if(AMEBAD_TFM_BOOT)
    target_compile_definitions(target_loader_km4 PRIVATE AMEBAD_TFM_BOOT=1)
endif()

add_custom_command(TARGET target_loader_km4 POST_BUILD
    COMMAND "${CMAKE_SIZE}" -A --radix=16 "$<TARGET_FILE:target_loader_km4>"
    COMMAND "${CMAKE_SIZE}" -t --radix=16 "$<TARGET_FILE:target_loader_km4>"
    COMMENT "section sizes for target_loader_km4"
    VERBATIM
)

# Stage 2: split ELF -> ram_1.bin / xip_boot.bin, prepend 32-byte headers,
# cat -> km4_boot_all.bin, optional RSIP encrypt @ 0x08004000. Helper is
# defined in component/amebad/image_tool/CMakeLists.txt.
amebad_make_boot_all_bin(target_loader_km4 km4_boot_all.bin 0x08004000)
