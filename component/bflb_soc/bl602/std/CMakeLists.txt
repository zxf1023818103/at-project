# bflb_soc_bl602_std — startup + SoC drivers for BL602
# (extracted from platform/bouffalo_sdk/drivers/soc/bl602/std).

set(BFLB_SOC_BL602_STD_SOURCES
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
    list(APPEND BFLB_SOC_BL602_STD_SOURCES src/bl602_romapi.c)
endif()

add_library(bflb_soc_bl602_std STATIC ${BFLB_SOC_BL602_STD_SOURCES})

target_include_directories(bflb_soc_bl602_std PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/include/hardware
)

target_compile_definitions(bflb_soc_bl602_std PUBLIC
    ARCH_RISCV
    BFLB_USE_HAL_DRIVER
    CONFIG_IRQ_NUM=80
)
if(CONFIG_ROMAPI)
    target_compile_definitions(bflb_soc_bl602_std PUBLIC BFLB_USE_ROM_DRIVER)
endif()

# Toolchain flags (BL602 = riscv-e24)
set(_march "rv32imafc")
set(_mabi "ilp32f")
target_compile_options(bflb_soc_bl602_std PUBLIC -march=${_march} -mabi=${_mabi})

if(TARGET sdk_intf_lib)
    target_compile_options(sdk_intf_lib INTERFACE -march=${_march} -mabi=${_mabi})
    target_link_options(sdk_intf_lib INTERFACE -march=${_march} -mabi=${_mabi})
    target_link_libraries(bflb_soc_bl602_std PRIVATE sdk_intf_lib)
endif()
