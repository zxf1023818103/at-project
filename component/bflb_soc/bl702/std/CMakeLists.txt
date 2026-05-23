# bflb_soc_bl702_std — startup + SoC drivers for BL702
# (extracted from platform/bouffalo_sdk/drivers/soc/bl702/std).

set(BFLB_SOC_BL702_STD_SOURCES
    startup/start.S
    startup/start_load.c
    startup/system_bl702.c
    startup/interrupt.c
    src/bl702_aon.c
    src/bl702_common.c
    src/bl702_clock.c
    src/bl702_ef_cfg.c
    src/bl702_glb.c
    src/bl702_hbn.c
    src/bl702_l1c.c
    src/bl702_pds.c
    src/bl702_pm.c
    src/bl702_sflash_ext.c
    src/bl702_xip_sflash_ext.c
    src/bl702_sf_cfg_ext.c
    port/bl702_clock.c
    port/bl702_reset.c
)

if(CONFIG_ROMAPI)
    list(APPEND BFLB_SOC_BL702_STD_SOURCES src/bl702_romapi.c)
endif()

add_library(bflb_soc_bl702_std STATIC ${BFLB_SOC_BL702_STD_SOURCES})

target_include_directories(bflb_soc_bl702_std PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/include/hardware
)

target_compile_definitions(bflb_soc_bl702_std PUBLIC
    ARCH_RISCV
    BFLB_USE_HAL_DRIVER
    CONFIG_IRQ_NUM=80
)
if(CONFIG_ROMAPI)
    target_compile_definitions(bflb_soc_bl702_std PUBLIC BFLB_USE_ROM_DRIVER)
endif()

set(_march "rv32imafc")
set(_mabi "ilp32f")
target_compile_options(bflb_soc_bl702_std PUBLIC -march=${_march} -mabi=${_mabi})

if(TARGET sdk_intf_lib)
    target_compile_options(sdk_intf_lib INTERFACE -march=${_march} -mabi=${_mabi})
    target_link_options(sdk_intf_lib INTERFACE -march=${_march} -mabi=${_mabi})
    target_link_libraries(bflb_soc_bl702_std PRIVATE sdk_intf_lib)
endif()
