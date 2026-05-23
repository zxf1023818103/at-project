# bflb_soc_bl616_std — startup + SoC drivers for BL616
# (extracted from platform/bouffalo_sdk/drivers/soc/bl616/std).

set(BFLB_SOC_BL616_STD_SOURCES
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
    list(APPEND BFLB_SOC_BL616_STD_SOURCES
        src/bl616_romapi_e907.c
        src/bl616_romapi_patch.c)
else()
    list(APPEND BFLB_SOC_BL616_STD_SOURCES
        src/bl616_glb_gpio.c
        src/bl616_glb.c
        src/bl616_hbn.c
        src/bl616_l1c.c
        src/bl616_pds.c
        src/bl616_aon.c)
endif()

if(CONFIG_RF)
    list(APPEND BFLB_SOC_BL616_STD_SOURCES
        src/bl616_mfg_efuse.c
        src/bl616_mfg_flash.c
        src/bl616_mfg_media.c)
endif()

add_library(bflb_soc_bl616_std STATIC ${BFLB_SOC_BL616_STD_SOURCES})

target_include_directories(bflb_soc_bl616_std PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/include/hardware
)

target_compile_definitions(bflb_soc_bl616_std PUBLIC
    ARCH_RISCV
    BFLB_USE_HAL_DRIVER
    CONFIG_IRQ_NUM=80
)
if(CONFIG_ROMAPI)
    target_compile_definitions(bflb_soc_bl616_std PUBLIC BFLB_USE_ROM_DRIVER)
endif()
if(CONFIG_XTAL_POWER_TYPE_ACTIVE)
    target_compile_definitions(bflb_soc_bl616_std PUBLIC CONFIG_XTAL_POWER_TYPE_ACTIVE)
endif()
if(CONFIG_PMP_NOT_INIT)
    target_compile_definitions(bflb_soc_bl616_std PUBLIC CONFIG_PMP_NO_INIT)
endif()

# Toolchain flags (BL616 = T-Head E907)
set(_march "rv32imafcpzpsfoperand_xtheade")
set(_mabi "ilp32f")
set(_mtune "e907")
target_compile_options(bflb_soc_bl616_std PUBLIC -march=${_march} -mabi=${_mabi} -mtune=${_mtune})

# Propagate march/mabi/mtune onto sdk_intf_lib so every other SDK lib + final link gets them.
if(TARGET sdk_intf_lib)
    target_compile_options(sdk_intf_lib INTERFACE -march=${_march} -mabi=${_mabi} -mtune=${_mtune})
    target_link_options(sdk_intf_lib INTERFACE -march=${_march} -mabi=${_mabi} -mtune=${_mtune})
    target_link_libraries(bflb_soc_bl616_std PRIVATE sdk_intf_lib)
endif()
