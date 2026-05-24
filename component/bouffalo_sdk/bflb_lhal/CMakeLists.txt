# bflb_lhal — Bouffalo Low-Level HAL (extracted from bouffalo_sdk/drivers/lhal).
#
# SDK -> native CMake mapping:
#   sdk_generate_library()                 -> add_library(<lib> STATIC)
#   sdk_library_add_sources(<f>)           -> target_sources(<lib> PRIVATE <f>)
#   sdk_add_include_directories(<d>)       -> target_include_directories(<lib> PUBLIC <d>)
#   sdk_add_private_include_directories    -> target_include_directories(<lib> PRIVATE <d>)
#   sdk_add_compile_definitions(<def>)     -> target_compile_definitions(<lib> PUBLIC <def>)
#   sdk_add_static_library(<path.a>)       -> target_link_libraries(<lib> PUBLIC <path.a>)
#
# Result: liblhal.a (target name `bflb_lhal`) with its public include / define
# surface propagated to anything that links against it.

if(NOT BOUFFALO_CHIP)
    message(FATAL_ERROR "bflb_lhal: BOUFFALO_CHIP not set (use a chip toolchain)")
endif()

add_library(bflb_lhal STATIC)

target_sources(bflb_lhal PRIVATE
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

if("${BOUFFALO_CHIP}" STREQUAL "bl602")
    target_sources(bflb_lhal PRIVATE
        src/bflb_adc.c
        src/bflb_acomp.c
        src/bflb_dac.c
        src/bflb_ir.c
        src/bflb_pwm_v1.c
        src/bflb_sdio2.c
        src/bflb_sec_gmac.c
    )
elseif("${BOUFFALO_CHIP}" STREQUAL "bl616")
    target_sources(bflb_lhal PRIVATE
        src/bflb_clock.c
        src/bflb_reset.c
        src/bflb_adc.c
        src/bflb_acomp.c
        src/bflb_dac.c
        src/bflb_emac.c
        src/bflb_ir.c
        src/bflb_mjpeg.c
        src/bflb_pwm_v2.c
        src/bflb_cam.c
        src/bflb_sdio2.c
        src/bflb_i2s.c
        src/bflb_dbi.c
        src/bflb_audac.c
        src/bflb_auadc.c
        src/bflb_wo.c
        src/bflb_sdh.c
        src/bflb_sec_gmac.c
    )
elseif("${BOUFFALO_CHIP}" STREQUAL "bl616cl")
    target_sources(bflb_lhal PRIVATE
        src/bflb_clock.c
        src/bflb_reset.c
        src/bflb_adc_v2.c
        src/bflb_emac.c
        src/bflb_mjpeg.c
        src/bflb_pwm_v2.c
        src/bflb_cam.c
        src/bflb_sdio2.c
        src/bflb_i2s.c
        src/bflb_dbi.c
        src/bflb_audac.c
        src/bflb_auadc.c
        src/bflb_wo.c
        src/bflb_sdh.c
        src/bflb_ir.c
        src/bflb_sec_gmac.c
        src/bflb_touch_v2.c
    )
endif()

if(CONFIG_CHERRYUSB AND NOT ("${BOUFFALO_CHIP}" STREQUAL "bl602"))
    target_sources(bflb_lhal PRIVATE src/bflb_usb_v2.c)
endif()

target_include_directories(bflb_lhal PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/arch"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/arch/risc-v/t-head"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/arch/risc-v/t-head/Core/Include"
    "${CMAKE_CURRENT_SOURCE_DIR}/config/${BOUFFALO_CHIP}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/flash"
)

if(NOT ("${BOUFFALO_CHIP}" STREQUAL "bl602"))
    target_sources(bflb_lhal PRIVATE
        include/arch/risc-v/t-head/rv_hart.c
        include/arch/risc-v/t-head/rv_pmp.c
    )
    if(NOT ("${BOUFFALO_CHIP}" STREQUAL "bl616cl"))
        target_include_directories(bflb_lhal PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/include/arch/risc-v/t-head/csi_dsp/include"
        )
        target_link_libraries(bflb_lhal PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/include/arch/risc-v/t-head/csi_dsp/lib/libcsi_xt900p32f_dsp.a"
        )
    endif()
endif()

# lhal #includes chip-specific register headers (bl602_glb.h, bl616_reset.h, …)
# from bl<chip>_std. PUBLIC so consumers of lhal can see them too.
# bflb_autoconf prepends -include autoconf.h to every TU.
target_link_libraries(bflb_lhal PUBLIC ${BOUFFALO_CHIP}_std bflb_autoconf)

target_sources(bflb_lhal PRIVATE config/${BOUFFALO_CHIP}/device_table.c)

# CHIP name macro (BL602 / BL616 / BL616CL) — public so consumers can #ifdef on it.
string(TOUPPER ${BOUFFALO_CHIP} _bflb_lhal_chipname)
target_compile_definitions(bflb_lhal PUBLIC ${_bflb_lhal_chipname})
unset(_bflb_lhal_chipname)

# Prebuilt PKA static library for the active chip.
target_link_libraries(bflb_lhal PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/pka/libpka_${BOUFFALO_CHIP}.a"
)

# Multimedia / PEC_V2 only available on bl616cl in this pruned tree.
if("${BOUFFALO_CHIP}" STREQUAL "bl616cl")
    if(CONFIG_PEC_V2)
        add_subdirectory(src/pec_v2)
        target_link_libraries(bflb_lhal PUBLIC bflb_lhal_pec_v2)
    endif()
    if(CONFIG_MULTIMEDIA_VIDEO)
        add_subdirectory(src/multimedia)
        target_link_libraries(bflb_lhal PUBLIC bflb_lhal_multimedia)
    endif()
endif()

# ROM API patch — bl616cl boots into a vendor ROM that overlays parts of the
# HAL; the patch source restores the symbols at runtime.
if("${BOUFFALO_CHIP}" STREQUAL "bl616cl")
    target_compile_definitions(bflb_lhal PUBLIC CONFIG_LHAL_ROMAPI)
    target_sources(bflb_lhal PRIVATE config/${BOUFFALO_CHIP}/bflb_lhal_romapi_patch.c)
endif()

if(CONFIG_LHAL_BOOTROM_ROMDRIVER)
    target_sources(bflb_lhal PRIVATE config/${BOUFFALO_CHIP}/bflb_lhal_romdriver.c)
endif()

# Version header: prefer VERSION file content; otherwise fall back to git describe
# (via the Git package's GIT_EXECUTABLE — no PATH lookup assumptions).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" VERSION_FILE_CONTENT)
    string(REGEX MATCH "LHAL_VERSION[ ]+\"([^\"]+)\"" _ ${VERSION_FILE_CONTENT})
    set(LHAL_VERSION "${CMAKE_MATCH_1}")
else()
    find_package(Git REQUIRED)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --abbrev=8 --tags --dirty --always
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
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
message(STATUS "LHAL Version: ${LHAL_VERSION}")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/config/lhal_version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/lhal_version.h"
)
target_include_directories(bflb_lhal PUBLIC
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_sources(bflb_lhal PRIVATE config/lhal_version.c)
