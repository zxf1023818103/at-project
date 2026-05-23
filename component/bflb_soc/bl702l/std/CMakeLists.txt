# bflb_soc_bl702l_std — startup + SoC drivers for BL702L
# (extracted from platform/bouffalo_sdk/drivers/soc/bl702l/std).

set(BFLB_SOC_BL702L_STD_SOURCES
    startup/start.S
    startup/start_load.c
    startup/system_bl702l.c
    startup/interrupt.c
    src/bl702l_ef_cfg.c
    src/bl702l_kys.c
    src/bl702l_pm.c
    src/bl702l_tzc_sec.c
    port/bl702l_clock.c
    port/bl702l_reset.c
)

if(CONFIG_ROMAPI)
    list(APPEND BFLB_SOC_BL702L_STD_SOURCES
        src/bl702l_romapi.c
        src/bl702l_romapi_patch.c)
else()
    list(APPEND BFLB_SOC_BL702L_STD_SOURCES
        src/bl702l_aon.c
        src/bl702l_clock.c
        src/bl702l_common.c
        src/bl702l_glb.c
        src/bl702l_hbn.c
        src/bl702l_l1c.c
        src/bl702l_pds.c)
endif()

add_library(bflb_soc_bl702l_std STATIC ${BFLB_SOC_BL702L_STD_SOURCES})

target_include_directories(bflb_soc_bl702l_std PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/include/hardware
)

target_compile_definitions(bflb_soc_bl702l_std PUBLIC
    ARCH_RISCV
    BFLB_USE_HAL_DRIVER
    CONFIG_IRQ_NUM=80
)
if(CONFIG_ROMAPI)
    target_compile_definitions(bflb_soc_bl702l_std PUBLIC BFLB_USE_ROM_DRIVER)
endif()

set(_march "rv32imafc")
set(_mabi "ilp32f")
target_compile_options(bflb_soc_bl702l_std PUBLIC -march=${_march} -mabi=${_mabi})

if(TARGET sdk_intf_lib)
    target_compile_options(sdk_intf_lib INTERFACE -march=${_march} -mabi=${_mabi})
    target_link_options(sdk_intf_lib INTERFACE -march=${_march} -mabi=${_mabi})
    target_link_libraries(bflb_soc_bl702l_std PRIVATE sdk_intf_lib)
endif()
