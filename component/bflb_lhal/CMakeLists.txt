# bflb_lhal — Bouffalo Lab Low-level HAL
# (extracted from platform/bouffalo_sdk/drivers/lhal).
# Spans all 6 chip families; large CHIP-conditional source list.

set(BFLB_LHAL_SOURCES
    src/bflb_common.c
    src/bflb_cks.c
    src/bflb_ef_ctrl.c
    src/bflb_gpio.c
    src/bflb_i2c.c
    src/bflb_dma.c
    src/bflb_rtc.c
    src/bflb_sec_mutex.c
    src/bflb_sec_aes.c
    src/bflb_sec_sha.c
    src/bflb_sec_trng.c
    src/bflb_spi.c
    src/bflb_timer.c
    src/bflb_uart.c
    src/bflb_wdg.c
    src/bflb_flash.c
    src/flash/bflb_sf_cfg.c
    src/flash/bflb_xip_sflash.c
    src/flash/bflb_sflash.c
    src/flash/bflb_sf_ctrl.c
    src/flash/bflb_flash_secreg_port.c
    src/flash/bflb_flash_secreg.c
    src/bflb_irq.c
    src/bflb_l1c.c
    src/bflb_mtimer.c
)

# Per-chip peripheral additions
if("${CHIP}" STREQUAL "bl602")
    list(APPEND BFLB_LHAL_SOURCES
        src/bflb_adc.c src/bflb_acomp.c src/bflb_dac.c src/bflb_ir.c
        src/bflb_pwm_v1.c src/bflb_sdio2.c src/bflb_sec_gmac.c)
elseif("${CHIP}" STREQUAL "bl702")
    list(APPEND BFLB_LHAL_SOURCES
        src/bflb_clock.c src/bflb_reset.c src/bflb_adc.c src/bflb_acomp.c
        src/bflb_dac.c src/bflb_emac.c src/bflb_ir.c src/bflb_pwm_v1.c
        src/bflb_cam.c src/bflb_spi_psram.c)
elseif("${CHIP}" STREQUAL "bl702l")
    list(APPEND BFLB_LHAL_SOURCES
        src/bflb_clock.c src/bflb_reset.c src/bflb_adc.c src/bflb_acomp.c
        src/bflb_kys.c src/bflb_pwm_v1.c src/bflb_pwm_v2.c src/bflb_spi_psram.c
        src/bflb_auadc.c src/bflb_ir.c src/bflb_sec_gmac.c)
elseif("${CHIP}" STREQUAL "bl616")
    list(APPEND BFLB_LHAL_SOURCES
        src/bflb_clock.c src/bflb_reset.c src/bflb_adc.c src/bflb_acomp.c
        src/bflb_dac.c src/bflb_emac.c src/bflb_ir.c src/bflb_mjpeg.c
        src/bflb_pwm_v2.c src/bflb_cam.c src/bflb_sdio2.c src/bflb_i2s.c
        src/bflb_dbi.c src/bflb_audac.c src/bflb_auadc.c src/bflb_wo.c
        src/bflb_sdh.c src/bflb_sec_gmac.c)
elseif("${CHIP}" STREQUAL "bl616cl")
    list(APPEND BFLB_LHAL_SOURCES
        src/bflb_clock.c src/bflb_reset.c src/bflb_adc_v2.c src/bflb_emac.c
        src/bflb_mjpeg.c src/bflb_pwm_v2.c src/bflb_cam.c src/bflb_sdio2.c
        src/bflb_i2s.c src/bflb_dbi.c src/bflb_audac.c src/bflb_auadc.c
        src/bflb_wo.c src/bflb_sdh.c src/bflb_ir.c src/bflb_sec_gmac.c
        src/bflb_touch_v2.c)
elseif("${CHIP}" STREQUAL "bl618dg")
    list(APPEND BFLB_LHAL_SOURCES
        src/bflb_clock.c src/bflb_reset.c src/bflb_adc_v3.c src/bflb_dac.c
        src/bflb_emac_v2.c src/bflb_ir.c src/bflb_mjpeg.c src/bflb_pwm_v2.c
        src/bflb_cam.c src/bflb_i2s.c src/bflb_dbi.c src/bflb_wo.c
        src/bflb_sdio3.c src/bflb_sdh.c src/bflb_ipc.c src/bflb_canfd.c
        src/bflb_dpi.c src/bflb_sec_gmac.c src/bflb_touch_v2.c
        src/bflb_auadc.c src/bflb_audac.c src/bflb_multi_core_sync.c)
endif()

# USB stack version select
if(CONFIG_CHERRYUSB)
    if("${CHIP}" STREQUAL "bl702")
        list(APPEND BFLB_LHAL_SOURCES src/bflb_usb_v1.c)
    elseif(("${CHIP}" STREQUAL "bl602") OR ("${CHIP}" STREQUAL "bl702l"))
        # no usb
    else()
        list(APPEND BFLB_LHAL_SOURCES src/bflb_usb_v2.c)
    endif()
endif()

# T-Head RISC-V hart + PMP for newer chips
if((NOT ("${CHIP}" STREQUAL "bl702"))
   AND (NOT ("${CHIP}" STREQUAL "bl602"))
   AND (NOT ("${CHIP}" STREQUAL "bl702l"))
)
    if((NOT ("${CHIP}" STREQUAL "bl618dg")) OR ("${CPU_MODEL}" STREQUAL "a0") OR ("${CPU_ID}" STREQUAL "lp"))
        list(APPEND BFLB_LHAL_SOURCES
            include/arch/risc-v/t-head/rv_hart.c
            include/arch/risc-v/t-head/rv_pmp.c)
    endif()
endif()

list(APPEND BFLB_LHAL_SOURCES config/${CHIP}/device_table.c)

# LHAL ROM patch (bl616cl / bl618dg ap)
if(("${CHIP}" STREQUAL "bl616cl") OR ("${CHIP}" STREQUAL "bl618dg" AND ("${CPU_ID}" STREQUAL "ap")))
    list(APPEND BFLB_LHAL_SOURCES config/${CHIP}/bflb_lhal_romapi_patch.c)
endif()

if(CONFIG_LHAL_BOOTROM_ROMDRIVER)
    list(APPEND BFLB_LHAL_SOURCES config/${CHIP}/bflb_lhal_romdriver.c)
endif()

# Version stamp
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/VERSION")
    file(READ "${CMAKE_CURRENT_LIST_DIR}/VERSION" VERSION_FILE_CONTENT)
    string(REGEX MATCH "LHAL_VERSION[ ]+\"([^\"]+)\"" _ ${VERSION_FILE_CONTENT})
    set(LHAL_VERSION "${CMAKE_MATCH_1}")
else()
    execute_process(
        COMMAND git describe --abbrev=8 --tags --dirty --always
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        OUTPUT_VARIABLE GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
    )
    if(GIT_TAG)
        set(LHAL_VERSION ${GIT_TAG})
    else()
        message(WARNING "No Valid version info found for LHAL!")
        set(LHAL_VERSION "version-unknown-panic")
    endif()
endif()
message(STATUS "bflb_lhal Version: ${LHAL_VERSION}")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/config/lhal_version.h.in"
    "${CMAKE_BINARY_DIR}/generated/lhal_version.h"
)
list(APPEND BFLB_LHAL_SOURCES config/lhal_version.c)

add_library(bflb_lhal STATIC ${BFLB_LHAL_SOURCES})

# Public include paths
target_include_directories(bflb_lhal PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/include/arch
    ${CMAKE_CURRENT_LIST_DIR}/include/arch/risc-v/t-head
    ${CMAKE_CURRENT_LIST_DIR}/include/arch/risc-v/t-head/Core/Include
    ${CMAKE_CURRENT_LIST_DIR}/config/${CHIP}
    ${CMAKE_CURRENT_LIST_DIR}/src/flash
    ${CMAKE_BINARY_DIR}/generated
)
if(("${CHIP}" STREQUAL "bl618dg") AND (NOT("${CPU_MODEL}" STREQUAL "a0")) AND (NOT("${CPU_ID}" STREQUAL "lp")))
    target_include_directories(bflb_lhal PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include/arch/risc-v/n900/Core/Include)
endif()

# CSI DSP prebuilt static lib (only for certain chip / cpu_id combos)
if((NOT ("${CHIP}" STREQUAL "bl702"))
   AND (NOT ("${CHIP}" STREQUAL "bl602"))
   AND (NOT ("${CHIP}" STREQUAL "bl702l"))
)
    if((NOT ("${CHIP}" STREQUAL "bl618dg")) OR ("${CPU_MODEL}" STREQUAL "a0") OR ("${CPU_ID}" STREQUAL "lp"))
        if((NOT ("${CPU_ID}" STREQUAL "d0")) AND (NOT ("${CPU_ID}" STREQUAL "lp")) AND (NOT("${CHIP}" STREQUAL "bl616cl")))
            target_include_directories(bflb_lhal PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include/arch/risc-v/t-head/csi_dsp/include)
            target_link_libraries(bflb_lhal PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include/arch/risc-v/t-head/csi_dsp/lib/libcsi_xt900p32f_dsp.a)
        endif()
    endif()
endif()

# CHIP define (uppercased)
string(TOUPPER ${CHIP} CHIPNAME)
target_compile_definitions(bflb_lhal PUBLIC ${CHIPNAME})
if(CPU_ID)
    string(TOUPPER ${CPU_ID} CPU_ID_NAME)
    target_compile_definitions(bflb_lhal PUBLIC CPU_${CPU_ID_NAME})
endif()

# PKA prebuilt
if((NOT ("${CPU_ID}" STREQUAL "d0")) AND (NOT ("${CPU_ID}" STREQUAL "lp")))
    if("${CPU_MODEL}" STREQUAL "b0")
        target_link_libraries(bflb_lhal PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src/pka/libpka_${CHIP}_b0.a)
    else()
        target_link_libraries(bflb_lhal PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src/pka/libpka_${CHIP}.a)
    endif()
endif()

# Optional subdirs for bl616cl / bl618dg
if(("${CHIP}" STREQUAL "bl616cl") OR ("${CHIP}" STREQUAL "bl618dg" AND (NOT ("${CPU_ID}" STREQUAL "lp"))))
    if(CONFIG_PEC_V2)
        add_subdirectory(src/pec_v2)
    endif()
    if(CONFIG_MULTIMEDIA_VIDEO)
        add_subdirectory(src/multimedia)
    endif()
endif()

if(("${CHIP}" STREQUAL "bl616cl") OR ("${CHIP}" STREQUAL "bl618dg" AND ("${CPU_ID}" STREQUAL "ap")))
    target_compile_definitions(bflb_lhal PUBLIC CONFIG_LHAL_ROMAPI)
endif()

if(TARGET sdk_intf_lib)
    target_link_libraries(bflb_lhal PRIVATE sdk_intf_lib)
endif()
