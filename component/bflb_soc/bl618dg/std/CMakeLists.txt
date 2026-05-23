# bflb_soc_bl618dg_std — startup + SoC drivers for BL618DG (multicore: AP/NP/LP)
# (extracted from platform/bouffalo_sdk/drivers/soc/bl618dg/std).

# Per-core startup subdir selection
if(NOT "${CPU_ID}" STREQUAL "lp")
    if("${CPU_MODEL}" STREQUAL "a0")
        set(STARTUP_SUB_DIR "mx")
    else()
        set(STARTUP_SUB_DIR "mn")
    endif()
else()
    set(STARTUP_SUB_DIR "lp")
endif()

set(BFLB_SOC_BL618DG_STD_SOURCES
    startup/${STARTUP_SUB_DIR}/start.S
    startup/${STARTUP_SUB_DIR}/vector.S
    startup/${STARTUP_SUB_DIR}/start_load.c
    startup/${STARTUP_SUB_DIR}/system_bl618dg.c
    startup/${STARTUP_SUB_DIR}/interrupt.c
    src/bl618dg_pm.c
    port/bl618dg_reset.c
)

if(NOT "${CPU_ID}" STREQUAL "lp")
    list(APPEND BFLB_SOC_BL618DG_STD_SOURCES startup/${STARTUP_SUB_DIR}/riscv_fpu.S)
endif()

if(("${CPU_ID}" STREQUAL "lp") OR ("${CPU_ID}" STREQUAL "np"))
    unset(CONFIG_ROMAPI CACHE)
    set(CONFIG_ROMAPI n CACHE INTERNAL "Override CONFIG_ROMAPI for bl618dg")
endif()

if(CONFIG_ROMAPI)
    list(APPEND BFLB_SOC_BL618DG_STD_SOURCES
        src/bl618dg_romapi_e907.c
        src/bl618dg_romapi_patch.c)
else()
    list(APPEND BFLB_SOC_BL618DG_STD_SOURCES
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
        port/bl618dg_clock.c)
endif()

if(CONFIG_RF)
    list(APPEND BFLB_SOC_BL618DG_STD_SOURCES
        src/bl618dg_mfg_efuse.c
        src/bl618dg_mfg_flash.c
        src/bl618dg_mfg_media.c)
endif()

if(CONFIG_STD_BOOTROM_ROMDRIVER)
    list(APPEND BFLB_SOC_BL618DG_STD_SOURCES src/bl618dg_romdriver_e907.c)
endif()

add_library(bflb_soc_bl618dg_std STATIC ${BFLB_SOC_BL618DG_STD_SOURCES})

target_include_directories(bflb_soc_bl618dg_std PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/include/hardware
)

target_compile_definitions(bflb_soc_bl618dg_std PUBLIC ARCH_RISCV BFLB_USE_HAL_DRIVER)
if(CONFIG_ROMAPI)
    target_compile_definitions(bflb_soc_bl618dg_std PUBLIC BFLB_USE_ROM_DRIVER)
endif()
if(CONFIG_USB_DISABLED)
    target_compile_definitions(bflb_soc_bl618dg_std PRIVATE CONFIG_USB_DISABLED)
endif()

# Per-core IRQ count
if(("${CPU_ID}" STREQUAL "ap") OR ("${CPU_ID}" STREQUAL "np"))
    if("${CPU_MODEL}" STREQUAL "a0")
        target_compile_definitions(bflb_soc_bl618dg_std PUBLIC CONFIG_IRQ_NUM=107)
    else()
        target_compile_definitions(bflb_soc_bl618dg_std PUBLIC CONFIG_IRQ_NUM=117)
    endif()
elseif("${CPU_ID}" STREQUAL "lp")
    target_compile_definitions(bflb_soc_bl618dg_std PUBLIC CONFIG_IRQ_NUM=112)
endif()

# Per-core toolchain march/mabi
if("${CPU_ID}" STREQUAL "lp")
    if("${CPU_MODEL}" STREQUAL "a0")
        set(_march "rv32emcxtheadse")
        set(_mabi "ilp32e")
    else()
        set(_march "rv32emc_zicsr")
        set(_mabi "ilp32e")
    endif()
    set(_mtune "e902")
else()
    if("${CPU_MODEL}" STREQUAL "a0")
        set(_march "rv32imafcpzpsfoperand_xtheade")
        set(_mabi "ilp32f")
        set(_mtune "e907")
    else()
        set(_march "rv32imafdc_zicsr_zifencei")
        set(_mabi "ilp32d")
        set(_mtune "nuclei-900-series")
    endif()
endif()

if("${CPU_MODEL}" STREQUAL "a0")
    target_compile_options(bflb_soc_bl618dg_std PUBLIC -march=${_march} -mabi=${_mabi} -mtune=${_mtune})
    if(TARGET sdk_intf_lib)
        target_compile_options(sdk_intf_lib INTERFACE -march=${_march} -mabi=${_mabi} -mtune=${_mtune})
        target_link_options(sdk_intf_lib INTERFACE -march=${_march} -mabi=${_mabi} -mtune=${_mtune})
    endif()
else()
    target_compile_options(bflb_soc_bl618dg_std PUBLIC -march=${_march} -mabi=${_mabi})
    if(TARGET sdk_intf_lib)
        target_compile_options(sdk_intf_lib INTERFACE -march=${_march} -mabi=${_mabi})
        target_link_options(sdk_intf_lib INTERFACE -march=${_march} -mabi=${_mabi})
    endif()
endif()

if(TARGET sdk_intf_lib)
    target_link_libraries(bflb_soc_bl618dg_std PRIVATE sdk_intf_lib)
endif()
