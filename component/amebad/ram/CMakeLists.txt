# amebad_ram — AmebaD RAM-resident HAL drivers, project-local copies of
# platform/ameba-rtos-d/.../fwlib/{ram_common,ram_lp,ram_hp}/. Flat folder; the
# SDK filename infix groups them. Four libs (target names kept so the bootloader
# / app link lists don't change):
#   amebad_ram_common      rtl8721d_*    shared HAL (UART/I2C/ADC/Flash/...), KM0
#   amebad_ram_lp          rtl8721dlp_*  KM0-only HAL (clk/pinmap/startup/...)
#   amebad_ram_hp          rtl8721dhp_*  KM4-only HAL (audio/i2s/sd/ota/...)
#   amebad_ram_common_km4  the few rtl8721d_* files the KM4 image2 needs but the
#                          HP ROM doesn't provide (ipc/gdma/flash/libc)
#
# Why KM4 doesn't just compile amebad_ram_common: the rest of the shared HAL
# comes from the HP ROM (rom_symbol table), so compiling it would DUPLICATE ROM
# symbols; also rtl8721d_qdec.c uses an LP-only RCC header. So KM4 takes only the
# surgical subset (amebad_ram_common_km4), which pulls no LP-only header.
#
# Resync when the submodule pointer moves:
#   diff -r platform/ameba-rtos-d/component/soc/realtek/amebad/fwlib/{ram_common,ram_lp,ram_hp}/ \
#           component/amebad/ram/

if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
elseif("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
else()
    return()
endif()

# Vendor-code warning suppression, shared by the libs below.
set(_ram_warnings
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)

if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    # Shared HAL — KM0 compiles all of it (KM4 pulls most from the HP ROM).
    add_library(amebad_ram_common STATIC
        rtl8721d_adc.c
        rtl8721d_bor.c
        rtl8721d_captouch.c
        rtl8721d_comparator.c
        rtl8721d_efuse.c
        rtl8721d_flash_ram.c
        rtl8721d_gdma_memcpy.c
        rtl8721d_gdma_ram.c
        rtl8721d_i2c.c
        rtl8721d_ipc_api.c
        rtl8721d_ir.c
        rtl8721d_keyscan.c
        rtl8721d_pll.c
        rtl8721d_qdec.c
        rtl8721d_ram_libc.c
        rtl8721d_rtc.c
        rtl8721d_sgpio.c
        rtl8721d_tim.c
        rtl8721d_uart.c
        rtl8721d_wdg.c
    )
    target_link_libraries(amebad_ram_common PUBLIC amebad_sdk_includes)
    target_compile_options(amebad_ram_common PRIVATE ${_ram_warnings})

    # KM0 LP-only HAL.
    add_library(amebad_ram_lp STATIC
        rtl8721dlp_app_start.c
        rtl8721dlp_clk.c
        rtl8721dlp_cpft.c
        rtl8721dlp_flashclk.c
        rtl8721dlp_km4.c
        rtl8721dlp_pinmap.c
        rtl8721dlp_simulation.c
        rtl8721dlp_startup.c
        rtl8721dlp_system.c
    )
    target_link_libraries(amebad_ram_lp PUBLIC amebad_sdk_includes)
    target_compile_options(amebad_ram_lp PRIVATE ${_ram_warnings})

elseif("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    # KM4 HP-only HAL.
    add_library(amebad_ram_hp STATIC
        rtl8721dhp_app_start.c
        rtl8721dhp_audio.c
        rtl8721dhp_clk.c
        rtl8721dhp_cpft.c
        rtl8721dhp_i2s.c
        rtl8721dhp_lcdc.c
        rtl8721dhp_ota_ram.c
        rtl8721dhp_sd.c
        rtl8721dhp_sdio.c
        rtl8721dhp_sdio_host.c
        rtl8721dhp_simulation.c
        rtl8721dhp_ssi.c
        rtl8721dhp_startup.c
        rtl8721dhp_system.c
    )
    target_link_libraries(amebad_ram_hp PUBLIC amebad_sdk_includes)
    target_compile_options(amebad_ram_hp PRIVATE ${_ram_warnings})

    # Subset of the shared HAL the KM4 image2 needs but the HP ROM doesn't give.
    add_library(amebad_ram_common_km4 STATIC
        rtl8721d_ipc_api.c
        rtl8721d_gdma_memcpy.c
        rtl8721d_gdma_ram.c
        rtl8721d_flash_ram.c
        rtl8721d_ram_libc.c
    )
    target_link_libraries(amebad_ram_common_km4 PUBLIC amebad_sdk_includes)
    target_compile_options(amebad_ram_common_km4 PRIVATE ${_ram_warnings})
endif()
