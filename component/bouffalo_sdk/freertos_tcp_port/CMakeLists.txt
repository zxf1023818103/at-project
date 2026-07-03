# component/bouffalo_sdk/freertos_tcp_port — Bouffalo wl80211 NetworkInterface for
# FreeRTOS-Plus-TCP (BL616 / BL616CL). The Bouffalo half of the lwIP -> FreeRTOS+TCP
# switch: the wl80211 WiFi driver <-> stack bridge + the two driver up-call symbols
# (wl80211_tcpip_input / wl80211_output_raw) the prebuilt libwl80211 references.
#
# The core stack + FreeRTOSIPConfig.h come from component/freertos-plus-tcp_config
# (freertos_plus_tcp), linked PUBLIC below. The stack and this bridge call into each
# other (stack -> NetworkInterface fns here; here -> FreeRTOS_IP APIs) and the driver
# calls wl80211_tcpip_input here, so the final image lists all of them inside one
# --start-group (bouffalo_sdk's link group).
#
# BL616 / BL616CL only (the wl80211 driver chips). EXCLUDE_FROM_ALL — pulled via the
# bouffalo_sdk umbrella.

if(NOT ("${BOUFFALO_CHIP}" STREQUAL "bl616" OR
        "${BOUFFALO_CHIP}" STREQUAL "bl616cl"))
    return()
endif()

add_library(bouffalo_freertos_tcp STATIC EXCLUDE_FROM_ALL
    bouffalo_netif.c
)

# Core stack + config + the upstream FreeRTOS+TCP include surface, and FreeRTOS itself.
target_link_libraries(bouffalo_freertos_tcp PUBLIC
    freertos_plus_tcp      # FreeRTOSIPConfig.h + the FreeRTOS+TCP core stack/API
    freertos_config        # FreeRTOSConfig.h + FreeRTOS.h include surface
)

# wl80211.h transitively #includes "queue/queue.h" + "tree/tree.h" (header-only
# utils), the same header deps the wl80211 lib itself links.
target_link_libraries(bouffalo_freertos_tcp PRIVATE bflb_queue bflb_tree)

# wl80211.h (struct iovec / ALIGN4_HI / WL80211_VIF_* / wl80211_tcpip_input proto).
# Headers only — the wl80211_mac_tx / platform_* symbols resolve at the final image
# link via bouffalo_sdk's --start-group, so do NOT link the wl80211 target (cycle).
target_include_directories(bouffalo_freertos_tcp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../wl80211/include"
)

target_compile_options(bouffalo_freertos_tcp PRIVATE
    -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-sign-compare)
