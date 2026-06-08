# amebad_usrcfg — AmebaD board / chip configuration data, project-local copies
# of platform/ameba-rtos-d/.../fwlib/usrcfg/. Three libs (target names kept so
# the bootloader / app link lists don't change):
#   amebad_usrcfg_common  board cfg shared by both cores (bootcfg/ipccfg/wificfg)
#   amebad_usrcfg_lp      KM0-only (flash/intf/pinmap/sleep)
#   amebad_usrcfg_hp      KM4-only (TrustZone/interface)
#
# Resync when the submodule pointer moves:
#   diff -r platform/ameba-rtos-d/component/soc/realtek/amebad/fwlib/usrcfg/ \
#           component/amebad/usrcfg/

if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
elseif("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
else()
    return()
endif()

# Vendor-code warning suppression, shared by the libs below.
set(_usrcfg_warnings
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)

# Chip-agnostic board config — built for both cores. OTA region macros
# (LS_IMG2_OTA{1,2}_ADDR) come from amebad_sdk_includes.
add_library(amebad_usrcfg_common STATIC
    rtl8721d_bootcfg.c
    rtl8721d_ipccfg.c
    rtl8721d_wificfg.c
)
target_link_libraries(amebad_usrcfg_common PUBLIC amebad_sdk_includes)
target_compile_options(amebad_usrcfg_common PRIVATE ${_usrcfg_warnings})

if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    add_library(amebad_usrcfg_lp STATIC
        rtl8721dlp_flashcfg.c
        rtl8721dlp_intfcfg.c
        rtl8721dlp_pinmapcfg.c
        rtl8721dlp_sleepcfg.c
    )
    target_link_libraries(amebad_usrcfg_lp PUBLIC amebad_sdk_includes)
    target_compile_options(amebad_usrcfg_lp PRIVATE ${_usrcfg_warnings})
elseif("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    add_library(amebad_usrcfg_hp STATIC
        rtl8721dhp_boot_trustzonecfg.c
        rtl8721dhp_intfcfg.c
    )
    target_link_libraries(amebad_usrcfg_hp PUBLIC amebad_sdk_includes)
    target_compile_options(amebad_usrcfg_hp PRIVATE ${_usrcfg_warnings})
    # Secure-world (IMG3) demo: select the TZ-on tz_config[] branch (image3 +
    # NSC regions). Surgical per-source define on trustzonecfg.c only — it pulls
    # __ram_image3_*/__psram_image3_* from the bootloader linker script and is
    # referenced only by the bootloader's BOOT_RAM_TZCfg, never by the app.
    if(AMEBAD_ENABLE_TZ_DEMO)
        set_source_files_properties(rtl8721dhp_boot_trustzonecfg.c PROPERTIES
            COMPILE_DEFINITIONS "CONFIG_TRUSTZONE_EN=1")
    endif()
endif()
