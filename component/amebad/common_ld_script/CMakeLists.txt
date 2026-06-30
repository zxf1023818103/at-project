# amebad_common_ld_script — AmebaD KM0-image shared linker-script fragments.
#
# Two SDK ld fragments that every KM0-image link stage needs identically,
# extracted here so each stage references one copy instead of carrying its
# own (and so the 2nd-stage bootloader stops reaching into bootloader_km0/ld/):
#
#   rlx8721d_layout.ld          flash + RAM MEMORY{} map. INCLUDE'd by name
#                               (no path) from every per-image link script:
#                                 bootloader_km0/ld/rlx8721d_boot.ld
#                                 bootloader_km0_2nd/ld/rlx8721d_boot_2nd.ld
#                                 app_km0/ld/rlx8721d_img2.ld   (once wired)
#                               GNU ld resolves a bare INCLUDE through its -L
#                               search paths, so this INTERFACE target adds
#                               this dir to the link line via
#                               target_link_directories(); any target that
#                               links amebad_common_ld_script can INCLUDE it.
#   rlx8721d_rom_symbol_acut.ld KM0 ROM symbol PROVIDE table. NOT INCLUDE'd —
#                               each stage cat's it (by absolute path) into its
#                               combined link script, so consumers just need the
#                               path, exposed via AMEBAD_COMMON_ROM_SYMBOL_LD.
#
# KM4's bootloader uses different fragments (rlx8721d_layout_tz.ld with the
# TrustZone-M split, rlx8721d_rom_symbol_acut_boot.ld) and intentionally does
# NOT consume this target.
#
# Project-local copies of the KM0-image (project_lp) ld fragments from
#   platform/ameba-rtos-d/.../project_lp/asdk/ld/
# Resync when the platform/ameba-rtos-d submodule pointer moves.

add_library(amebad_common_ld_script INTERFACE)

# -L <this dir> so `INCLUDE "rlx8721d_layout.ld"` resolves in any consumer.
target_link_directories(amebad_common_ld_script INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}")

# Absolute paths so consumers can list them in their linker-script
# add_custom_command DEPENDS (relink when a fragment changes) and cat
# rom_symbol_acut.ld directly. CACHE INTERNAL → visible across subdirs.
set(AMEBAD_COMMON_LAYOUT_LD
    "${CMAKE_CURRENT_SOURCE_DIR}/rlx8721d_layout.ld"
    CACHE INTERNAL "Shared AmebaD KM0-image flash/RAM layout linker script")
set(AMEBAD_COMMON_ROM_SYMBOL_LD
    "${CMAKE_CURRENT_SOURCE_DIR}/rlx8721d_rom_symbol_acut.ld"
    CACHE INTERNAL "Shared AmebaD KM0 ROM-symbol PROVIDE linker script")
