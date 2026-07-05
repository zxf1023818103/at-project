# macsw — Bouffalo's WiFi6 MAC software layer for BL616 / BL616CL,
# extracted from bouffalo_sdk/components/wireless/macsw/ plus the
# adjacent macsw_plat.c that lived at the wireless/ root.
#
# Layout:
#   inc/                MAC SW public headers (bl_fw_api.h, mac_frame.h,
#                       macsw.h, macsw_${variant}_config.h, ieee80211.h…)
#   lib/                prebuilt .a (per chip × variant)
#   macsw_plat.c        platform shim (was wireless/macsw_plat.c upstream;
#                       built once per project, pulls in
#                       CONFIG_MACSW_SELECT_INCLUDE which decides which
#                       macsw_${variant}_config.h gets #include'd)
#   macsw_cache_affinity.ld.in   linker fragment template (unused today)
#
# Config: CONFIG_MACSW_SELECT picks the variant — default / coex / bridge /
# ack on bl616; default / ack on bl616cl. This fork only ships the
# `default` prebuilts; add more libs to lib/ + extend the FATAL_ERROR
# message below if other variants are needed.

if(NOT ("${BOUFFALO_CHIP}" STREQUAL "bl616" OR
        "${BOUFFALO_CHIP}" STREQUAL "bl616cl"))
    message(FATAL_ERROR
        "macsw: only bl616 / bl616cl supported "
        "(bl618dg / bl616_bridge variants pruned).")
endif()

if(NOT DEFINED CONFIG_MACSW_SELECT)
    set(CONFIG_MACSW_SELECT "default")
endif()

set(_macsw_lib        "${CMAKE_CURRENT_SOURCE_DIR}/lib/libmacsw_${BOUFFALO_CHIP}_${CONFIG_MACSW_SELECT}.a")
set(_macsw_config_lib "${CMAKE_CURRENT_SOURCE_DIR}/lib/libmacsw_config_${BOUFFALO_CHIP}_${CONFIG_MACSW_SELECT}.a")
foreach(_lib IN ITEMS "${_macsw_lib}" "${_macsw_config_lib}")
    if(NOT EXISTS "${_lib}")
        message(FATAL_ERROR
            "macsw: missing prebuilt ${_lib} "
            "(CONFIG_MACSW_SELECT='${CONFIG_MACSW_SELECT}'; "
            "available: default).")
    endif()
endforeach()

add_library(macsw STATIC macsw_plat.c)

target_include_directories(macsw PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/inc"
)

target_compile_definitions(macsw PUBLIC
    "CONFIG_MACSW_SELECT_INCLUDE=\"macsw_${CONFIG_MACSW_SELECT}_config.h\""
)

target_link_libraries(macsw PUBLIC
    "${_macsw_lib}"
    "${_macsw_config_lib}"
    bflb_lhal
    ${BOUFFALO_CHIP}_std
    freertos_port
    # macsw.h pulls phy.h / wl_api.h from bl${chip}_phyrf
    ${BOUFFALO_CHIP}_phyrf
    # macsw_plat.c pulls bl_lp.h from bflb_sys
    bflb_sys
)

unset(_macsw_lib)
unset(_macsw_config_lib)
