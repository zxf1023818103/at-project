# bflb_sys — SoC sys layer (chip-specific low-power, init, recovery)
# (extracted from platform/bouffalo_sdk/drivers/sys).

set(BFLB_SYS_SOURCES ${CHIP}/${CHIP}_sys.c)

string(TOLOWER "${CHIP}" CHIP_LOWER)
if(CHIP_LOWER MATCHES "^(bl616|bl616cl|bl618dg)$")
    list(APPEND BFLB_SYS_SOURCES
        bl_lp_callback.c
        bl_lp_bcn.c
        bl_pm_wifi.c
        bl_lp_fw_image.c)
    if("${CHIP}" STREQUAL "bl618dg")
        list(APPEND BFLB_SYS_SOURCES
            ${CHIP}/${CHIP}_lp.c
            ${CHIP}/${CHIP}_lp_callback.c
            ${CHIP}/${CHIP}_lp_wakeup.c
            ${CHIP}/${CHIP}_lp_bcn_tbl.c
            ${CHIP}/${CHIP}_xip_recovery.c
            ${CHIP}/${CHIP}_app_pds_recovery.c
            ${CHIP}/${CHIP}_32k_trim.c)
    else()
        list(APPEND BFLB_SYS_SOURCES ${CHIP}/${CHIP}_lp.c)
        if("${CHIP}" STREQUAL "bl616")
            list(APPEND BFLB_SYS_SOURCES
                ${CHIP}/${CHIP}_lp_callback.c
                ${CHIP}/${CHIP}_lp_fw_image.c
                ${CHIP}/${CHIP}_lp_wakeup.c
                ${CHIP}/${CHIP}_lp_wifi.c)
        endif()
        if("${CHIP}" STREQUAL "bl616cl")
            list(APPEND BFLB_SYS_SOURCES
                ${CHIP}/${CHIP}_ext_dcdc.c
                ${CHIP}/${CHIP}_lp_callback.c
                ${CHIP}/${CHIP}_lp_wakeup.c
                ${CHIP}/${CHIP}_lp_bcn_tbl.c
                ${CHIP}/${CHIP}_app_pds_recovery.c
                ${CHIP}/${CHIP}_psram_recovery.c)
        endif()
        list(APPEND BFLB_SYS_SOURCES
            ${CHIP}/${CHIP}_xip_recovery.c
            ${CHIP}/${CHIP}_32k_trim.c)
    endif()
endif()

if("${CHIP}" STREQUAL "bl702" AND CONFIG_PM)
    list(APPEND BFLB_SYS_SOURCES
        bl702/bl702_lp.c
        bl702/bl702_xip_recovery.c
        bl702/bl702_lp_debug.c)
endif()

add_library(bflb_sys STATIC ${BFLB_SYS_SOURCES})

target_include_directories(bflb_sys PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/${CHIP}
)

if(TARGET sdk_intf_lib)
    target_link_libraries(bflb_sys PRIVATE sdk_intf_lib)
endif()
