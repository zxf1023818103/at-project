# bl616_std — BL616 chip-specific startup + register/peripheral driver layer.
# Extracted from bouffalo_sdk/drivers/soc/bl616/std.
#
# -march / -mabi / -mtune were dropped: the chip toolchain
# (cmake/bl616-toolchain.cmake) already pins them via CMAKE_<LANG>_FLAGS_INIT.

add_library(bl616_std STATIC
    startup/start.S
    startup/vector.S
    startup/riscv_fpu.S
    startup/start_load.c
    startup/system_bl616.c
    startup/interrupt.c

    src/bl616_clock.c
    src/bl616_common.c
    src/bl616_ef_cfg.c
    src/bl616_sdh.c
    src/bl616_sdu.c
    src/bl616_tzc_sec.c
    src/bl616_psram.c
    src/bl616_pm.c
    src/bl616_sec_dbg.c

    port/bl616_gpio.c
    port/bl616_clock.c
    port/bl616_reset.c
)

if(CONFIG_ROMAPI)
    target_sources(bl616_std PRIVATE
        src/bl616_romapi_e907.c
        src/bl616_romapi_patch.c
    )
    target_compile_definitions(bl616_std PUBLIC BFLB_USE_ROM_DRIVER)
else()
    target_sources(bl616_std PRIVATE
        src/bl616_glb_gpio.c
        src/bl616_glb.c
        src/bl616_hbn.c
        src/bl616_l1c.c
        src/bl616_pds.c
        src/bl616_aon.c
    )
endif()

if(CONFIG_RF)
    target_sources(bl616_std PRIVATE
        src/bl616_mfg_efuse.c
        src/bl616_mfg_flash.c
        src/bl616_mfg_media.c
    )
endif()

if(CONFIG_XTAL_POWER_TYPE_ACTIVE)
    target_compile_definitions(bl616_std PUBLIC CONFIG_XTAL_POWER_TYPE_ACTIVE)
endif()

# Note: the SDK uses CONFIG_PMP_NOT_INIT as the input flag but defines
# CONFIG_PMP_NO_INIT — preserved verbatim.
if(CONFIG_PMP_NOT_INIT)
    target_compile_definitions(bl616_std PUBLIC CONFIG_PMP_NO_INIT)
endif()

target_include_directories(bl616_std PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/hardware"
)

target_compile_definitions(bl616_std PUBLIC
    ARCH_RISCV
    BFLB_USE_HAL_DRIVER
    CONFIG_IRQ_NUM=80
)

# Circular header dependency with bflb_lhal: std sources include lhal headers
# (bflb_core.h, csr.h, bflb_efuse.h) and lhal sources include std headers
# (bl616_glb.h). Both directions linked PUBLIC; CMake duplicates static libs
# on the link line to resolve symbols across the cycle.
target_link_libraries(bl616_std PUBLIC bflb_lhal)
