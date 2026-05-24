# bl602_std — BL602 chip-specific startup + register/peripheral driver layer.
# Extracted from bouffalo_sdk/drivers/soc/bl602/std.
#
# -march / -mabi were dropped: the chip toolchain (cmake/bl602-toolchain.cmake)
# already pins them via CMAKE_<LANG>_FLAGS_INIT.

add_library(bl602_std STATIC
    startup/start.S
    startup/start_load.c
    startup/system_bl602.c
    startup/interrupt.c

    src/bl602_aon.c
    src/bl602_common.c
    src/bl602_clock.c
    src/bl602_ef_cfg.c
    src/bl602_glb.c
    src/bl602_hbn.c
    src/bl602_l1c.c
    src/bl602_pds.c
    src/bl602_tzc_sec.c
    src/bl602_sflash_ext.c
    src/bl602_xip_sflash_ext.c
    src/bl602_sf_cfg_ext.c
    src/bl602_mfg_efuse.c
    src/bl602_mfg_media.c
    src/bl602_ef_ctrl.c
    src/bl602_pm.c

    port/bl602_clock.c
)

if(CONFIG_ROMAPI)
    target_sources(bl602_std PRIVATE src/bl602_romapi.c)
    target_compile_definitions(bl602_std PUBLIC BFLB_USE_ROM_DRIVER)
endif()

target_include_directories(bl602_std PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/hardware"
)

target_compile_definitions(bl602_std PUBLIC
    ARCH_RISCV
    BFLB_USE_HAL_DRIVER
    CONFIG_IRQ_NUM=80
)

# Circular header dependency with bflb_lhal: std sources include lhal headers
# (bflb_core.h, csr.h, bflb_efuse.h) and lhal sources include std headers
# (bl602_glb.h). Both directions linked PUBLIC; CMake duplicates static libs
# on the link line to resolve symbols across the cycle.
target_link_libraries(bl602_std PUBLIC bflb_lhal)
