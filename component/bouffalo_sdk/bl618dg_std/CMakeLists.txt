# bl618dg_std — BL618DG chip-specific startup + register/peripheral driver layer.
# Extracted from bouffalo_sdk/drivers/soc/bl618dg/std.
#
# -march / -mabi were dropped: the core toolchain (cmake/bl618dg-{ap,np}-toolchain.cmake)
# already pins them via CMAKE_<LANG>_FLAGS_INIT. B0 silicon passes no -mtune.
#
# Only the `mn` startup variant is vendored: it is the one B0 (Nuclei-900) uses.
# `mx` is the A-series/e907 variant and `lp` is the LP core — neither is built
# here (this project targets the mass-production B0 part, AP + NP only).
#
# The two cores build DIFFERENT source sets, mirroring the SDK's own split
# (verified against an SDK dualcore build's object list):
#   AP  -> ROM API path      (romapi_e907 + romapi_patch)
#   NP  -> full register drivers (aon/clock/common/ef_cfg/glb/hbn/pds/psram/
#          sec_dbg/tzc_sec + port/clock); the SDK force-sets CONFIG_ROMAPI=n
#          for the NP core.
# Despite the `_e907` suffix the ROM API file is the chip's ROM table and is
# what the SDK builds for B0's AP core too.

if(NOT BOUFFALO_CPU_ID)
    message(FATAL_ERROR
        "bl618dg_std: BOUFFALO_CPU_ID is not set. Configure with one of "
        "cmake/bl618dg-ap-toolchain.cmake or cmake/bl618dg-np-toolchain.cmake.")
endif()

add_library(bl618dg_std STATIC
    startup/mn/start.S
    startup/mn/vector.S
    startup/mn/riscv_fpu.S
    startup/mn/start_load.c
    startup/mn/system_bl618dg.c
    startup/mn/interrupt.c

    src/bl618dg_pm.c
    port/bl618dg_reset.c
)

if(BOUFFALO_CPU_ID STREQUAL "ap")
    # ROM API path (CONFIG_ROMAPI defaults on for the AP core).
    target_sources(bl618dg_std PRIVATE
        src/bl618dg_romapi_e907.c
        src/bl618dg_romapi_patch.c
    )
    target_compile_definitions(bl618dg_std PUBLIC BFLB_USE_ROM_DRIVER)
else()
    # NP core: the SDK forces CONFIG_ROMAPI=n, so the register drivers are built.
    target_sources(bl618dg_std PRIVATE
        src/bl618dg_aon.c
        src/bl618dg_clock.c
        src/bl618dg_common.c
        src/bl618dg_ef_cfg.c
        src/bl618dg_glb.c
        src/bl618dg_hbn.c
        src/bl618dg_pds.c
        src/bl618dg_psram.c
        src/bl618dg_sec_dbg.c
        src/bl618dg_tzc_sec.c
        port/bl618dg_clock.c
    )
endif()

if(CONFIG_RF)
    target_sources(bl618dg_std PRIVATE
        src/bl618dg_mfg_efuse.c
        src/bl618dg_mfg_flash.c
        src/bl618dg_mfg_media.c
    )
endif()

if(CONFIG_STD_BOOTROM_ROMDRIVER)
    target_sources(bl618dg_std PRIVATE src/bl618dg_romdriver_e907.c)
endif()

if(CONFIG_USB_DISABLED)
    target_compile_definitions(bl618dg_std PRIVATE CONFIG_USB_DISABLED)
endif()

target_include_directories(bl618dg_std PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/hardware"
)

# CONFIG_IRQ_NUM=117 is the B0 value (A-series/a0 uses 107) for both AP and NP.
target_compile_definitions(bl618dg_std PUBLIC
    ARCH_RISCV
    BFLB_USE_HAL_DRIVER
    CONFIG_IRQ_NUM=117
)

# Circular header dependency with bflb_lhal: std sources include lhal headers
# (bflb_core.h, csr.h, bflb_efuse.h) and lhal sources include std headers
# (bl618dg_glb.h). Both directions linked PUBLIC; CMake duplicates static libs
# on the link line to resolve symbols across the cycle.
target_link_libraries(bl618dg_std PUBLIC bflb_lhal)
