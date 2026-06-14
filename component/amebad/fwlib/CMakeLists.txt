# amebad_fwlib — Realtek AmebaD (RTL8721D) chip fwlib: headers + ROM-symbol
# linker scripts + the RAM-resident HAL driver sources, in one component.
# (Merge of the former fwlib_headers/ and ram/ — both were extracts of the same
# SDK subtree platform/ameba-rtos-d/.../fwlib/: include/ -> the headers here,
# {ram_common,ram_lp,ram_hp}/ -> the rtl8721d*_*.c here.)
#
# Produces:
#   amebad_fwlib_headers  INTERFACE — 69 .h (chip regs, HAL inlines, the
#                         ameba_soc.h umbrella every AmebaD source pulls in).
#                         SYSTEM so -Wall/-Wextra don't fire on vendor headers.
#   AMEBAD_FWLIB_ROM_KM{4,0}_LD  cache vars -> fwlib_rom_km{4,0}.ld: the mask-ROM
#                         addresses of the HAL functions these headers declare
#                         (UART_/GPIO_/FLASH_/RCC_/GDMA_/EFUSE_/CPU_ClkGet/Set
#                         DiagPrintf ...), partitioned out of the per-core
#                         rlx8721d_rom_symbol_acut.ld. KM4 addrs != KM0 addrs.
#   amebad_ram_common / _lp / _hp  STATIC — the RAM HAL the ROM doesn't provide.
#
# Resync when the submodule pointer moves:
#   diff -r platform/ameba-rtos-d/component/soc/realtek/amebad/fwlib/include/ \
#           component/amebad/fwlib/        # headers
#   diff -r platform/ameba-rtos-d/.../fwlib/{ram_common,ram_lp,ram_hp}/ \
#           component/amebad/fwlib/        # sources

# ---- Headers + ROM-symbol linker scripts (both cores need these) ----
add_library(amebad_fwlib_headers INTERFACE)
target_include_directories(amebad_fwlib_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

set(AMEBAD_FWLIB_ROM_KM4_LD "${CMAKE_CURRENT_SOURCE_DIR}/fwlib_rom_km4.ld"
    CACHE INTERNAL "AmebaD KM4 (HP) fwlib HAL ROM-symbol PROVIDE linker script")
set(AMEBAD_FWLIB_ROM_KM0_LD "${CMAKE_CURRENT_SOURCE_DIR}/fwlib_rom_km0.ld"
    CACHE INTERNAL "AmebaD KM0 (LP) fwlib HAL ROM-symbol PROVIDE linker script")

# ---- RAM-resident HAL drivers (chip-gated) ----
# Three lib names, kept so the bootloader / app link lists don't change:
#   amebad_ram_common  rtl8721d_*    shared HAL — KM0 compiles all 20; KM4
#                                    compiles only the few the HP ROM doesn't
#                                    provide (ipc/gdma/flash/libc). KM0 and KM4
#                                    are separate builds, so the same target name
#                                    holds the full set on KM0 / the subset on KM4.
#   amebad_ram_lp      rtl8721dlp_*  KM0-only HAL (clk/pinmap/startup/...)
#   amebad_ram_hp      rtl8721dhp_*  KM4-only HAL (audio/i2s/sd/ota/...)
# Why KM4 doesn't compile all of ram_common: the rest of the shared HAL comes
# from the HP ROM (rom_symbol table), so compiling it would DUPLICATE ROM symbols;
# also rtl8721d_qdec.c uses an LP-only RCC header. So KM4's amebad_ram_common is
# the surgical subset that pulls no LP-only header.
#
# Note: amebad_sdk_includes (linked below) is created by a later add_subdirectory
# (sdk_includes) — a cross-directory forward reference CMake resolves at generate.

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
    # Same target name as KM0's full set — they're never in the same build.
    add_library(amebad_ram_common STATIC
        rtl8721d_ipc_api.c
        rtl8721d_gdma_memcpy.c
        rtl8721d_gdma_ram.c
        rtl8721d_flash_ram.c
        rtl8721d_ram_libc.c
        rtl8721d_wdg.c          # WDG HAL — not in the HP ROM; the KM4 console `reboot`
                                # (and mbed wdt_api/sys_api) need it for a watchdog reset.
    )
    target_link_libraries(amebad_ram_common PUBLIC amebad_sdk_includes)
    target_compile_options(amebad_ram_common PRIVATE ${_ram_warnings})
endif()
