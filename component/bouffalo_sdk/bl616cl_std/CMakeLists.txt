# bl616cl_std — BL616CL chip-specific startup + register/peripheral driver layer.
# Extracted from bouffalo_sdk/drivers/soc/bl616cl/std.
#
# -march / -mabi / -mtune were dropped: the chip toolchain
# (cmake/bl616cl-toolchain.cmake) already pins them via CMAKE_<LANG>_FLAGS_INIT.

add_library(bl616cl_std STATIC
    startup/start.S
    startup/vector.S
    startup/riscv_fpu.S
    startup/start_load.c
    startup/system_bl616cl.c
    startup/interrupt.c

    src/bl616cl_sec_dbg.c
    src/bl616cl_tzc_sec.c
    src/bl616cl_psram.c
    src/bl616cl_pm.c

    port/bl616cl_clock.c
    port/bl616cl_reset.c
)

if(CONFIG_ROMAPI)
    target_sources(bl616cl_std PRIVATE
        src/bl616cl_romapi_e907.c
        src/bl616cl_romapi_patch.c
    )
    target_compile_definitions(bl616cl_std PUBLIC BFLB_USE_ROM_DRIVER)
else()
    target_sources(bl616cl_std PRIVATE
        src/bl616cl_aon.c
        src/bl616cl_clock.c
        src/bl616cl_common.c
        src/bl616cl_ef_cfg.c
        src/bl616cl_glb.c
        src/bl616cl_hbn.c
        src/bl616cl_pds.c
    )
endif()

if(CONFIG_RF)
    target_sources(bl616cl_std PRIVATE
        src/bl616cl_mfg_efuse.c
        src/bl616cl_mfg_flash.c
        src/bl616cl_mfg_media.c
    )
endif()

if(CONFIG_STD_BOOTROM_ROMDRIVER)
    target_sources(bl616cl_std PRIVATE src/bl616cl_romdriver_e907.c)
endif()

if(CONFIG_USB_DISABLED)
    target_compile_definitions(bl616cl_std PRIVATE CONFIG_USB_DISABLED)
endif()

target_include_directories(bl616cl_std PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/hardware"
)

target_compile_definitions(bl616cl_std PUBLIC
    ARCH_RISCV
    BFLB_USE_HAL_DRIVER
    CONFIG_IRQ_NUM=83
)

# Circular header dependency with bflb_lhal: std sources include lhal headers
# (bflb_core.h, csr.h, bflb_efuse.h) and lhal sources include std headers
# (bl616cl_glb.h). Both directions linked PUBLIC; CMake duplicates static libs
# on the link line to resolve symbols across the cycle.
target_link_libraries(bl616cl_std PUBLIC bflb_lhal)
