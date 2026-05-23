# bflb_soc_bl616cl_std — startup + SoC drivers for BL616CL
# (extracted from platform/bouffalo_sdk/drivers/soc/bl616cl/std).

set(BFLB_SOC_BL616CL_STD_SOURCES
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
    list(APPEND BFLB_SOC_BL616CL_STD_SOURCES
        src/bl616cl_romapi_e907.c
        src/bl616cl_romapi_patch.c)
else()
    list(APPEND BFLB_SOC_BL616CL_STD_SOURCES
        src/bl616cl_aon.c
        src/bl616cl_clock.c
        src/bl616cl_common.c
        src/bl616cl_ef_cfg.c
        src/bl616cl_glb.c
        src/bl616cl_hbn.c
        src/bl616cl_pds.c)
endif()

if(CONFIG_RF)
    list(APPEND BFLB_SOC_BL616CL_STD_SOURCES
        src/bl616cl_mfg_efuse.c
        src/bl616cl_mfg_flash.c
        src/bl616cl_mfg_media.c)
endif()

if(CONFIG_STD_BOOTROM_ROMDRIVER)
    list(APPEND BFLB_SOC_BL616CL_STD_SOURCES src/bl616cl_romdriver_e907.c)
endif()

add_library(bflb_soc_bl616cl_std STATIC ${BFLB_SOC_BL616CL_STD_SOURCES})

target_include_directories(bflb_soc_bl616cl_std PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/include/hardware
)

target_compile_definitions(bflb_soc_bl616cl_std PUBLIC
    ARCH_RISCV
    BFLB_USE_HAL_DRIVER
    CONFIG_IRQ_NUM=83
)
if(CONFIG_ROMAPI)
    target_compile_definitions(bflb_soc_bl616cl_std PUBLIC BFLB_USE_ROM_DRIVER)
endif()
if(CONFIG_USB_DISABLED)
    target_compile_definitions(bflb_soc_bl616cl_std PRIVATE CONFIG_USB_DISABLED)
endif()

# Toolchain flags (BL616CL = T-Head E907 no zpsfoperand)
set(_march "rv32imafc_xtheade")
set(_mabi "ilp32f")
set(_mtune "e907")
target_compile_options(bflb_soc_bl616cl_std PUBLIC -march=${_march} -mabi=${_mabi} -mtune=${_mtune})

if(TARGET sdk_intf_lib)
    target_compile_options(sdk_intf_lib INTERFACE -march=${_march} -mabi=${_mabi} -mtune=${_mtune})
    target_link_options(sdk_intf_lib INTERFACE -march=${_march} -mabi=${_mabi} -mtune=${_mtune})
    target_link_libraries(bflb_soc_bl616cl_std PRIVATE sdk_intf_lib)
endif()
