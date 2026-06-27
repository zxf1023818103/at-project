# AmebaD executable subtree (the lib/executable split: the libraries live in component/amebad, the
# firmware IMAGES live here). Entered from the ROOT CMakeLists via add_subdirectory(project/amebad),
# gated on AMEBAD_CHIP, AFTER add_subdirectory(component) - so the AmebaD lib targets, image_tool's
# CMake functions (amebad_make_*), and common_ld_script's AMEBAD_COMMON_*_LD cache vars are all in scope.
#
# Each app/bootloader links the component/amebad lib targets (amebad_sdk_includes, amebad_ram_*,
# amebad_usrcfg_*, amebad_wifi, amebad_wlan, ...) + the cross-platform config targets, and calls the
# image_tool packaging functions in its POST_BUILD. app_km{0,4} also DEFINE amebad_proj_config (the
# per-core platform_opts/autoconf/main.h include surface) that the lib-side amebad_sdk_includes links -
# a forward reference resolved at generate time (same configure).

if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    add_subdirectory(bootloader_km0)
    add_subdirectory(bootloader_km0_2nd)   # opt-in via AMEBAD_ENABLE_2ND_BOOT
    add_subdirectory(app_km0)              # KM0 LP application image (image2)
elseif("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    add_subdirectory(bootloader_km4)
    add_subdirectory(app_km4)              # KM4 HP application image (image2)
    add_subdirectory(app_km4_secure)       # KM4 secure-world: TF-M SPE (ExternalProject), opt-in AMEBAD_TFM_BOOT
endif()

# Cross-core final image. `cmake --build <km0-or-km4-build-dir> -t amebad_image_all` runs
# build_image_all.py to (re)build both cores and assemble the flashable Image_All.bin. Defined in both
# core configures; invoke from either.
amebad_make_image_all_bin()
