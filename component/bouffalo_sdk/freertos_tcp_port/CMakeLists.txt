# component/bouffalo_sdk/freertos_tcp_port — Bouffalo WiFi driver <-> FreeRTOS-Plus-TCP
# NetworkInterface bridge. The Bouffalo half of the lwIP -> FreeRTOS+TCP switch.
#   BL616 / BL616CL: bouffalo_netif.c — wl80211 driver (re-supplies wl80211_tcpip_input /
#     wl80211_output_raw, the up-call symbols the prebuilt libwl80211 references).
#   BL602:           bl602_netif.c — wifi4 driver (the driver's bl_utils.c RX +
#     bl_tx.c TX call into the bl602_tcp_* / bl602_ll_output helpers here).
#
# The core stack + FreeRTOSIPConfig.h come from component/freertos-plus-tcp_config
# (freertos_plus_tcp), linked PUBLIC. The stack and this bridge call into each other,
# and the driver calls into it, so the final image lists all of them inside one
# --start-group (bouffalo_sdk's link group). EXCLUDE_FROM_ALL — pulled via the umbrella.

if("${BOUFFALO_CHIP}" STREQUAL "bl616" OR "${BOUFFALO_CHIP}" STREQUAL "bl616cl")
    set(_bflb_tcp_src bouffalo_netif.c)   # wl80211 driver
elseif("${BOUFFALO_CHIP}" STREQUAL "bl602")
    set(_bflb_tcp_src bl602_netif.c)      # bl60x wifi4 driver
else()
    return()
endif()

add_library(bouffalo_freertos_tcp STATIC EXCLUDE_FROM_ALL
    ${_bflb_tcp_src}
)

# Core stack + config + the upstream FreeRTOS+TCP include surface, and FreeRTOS itself.
target_link_libraries(bouffalo_freertos_tcp PUBLIC
    freertos_plus_tcp      # FreeRTOSIPConfig.h + the FreeRTOS+TCP core stack/API
    freertos_config        # FreeRTOSConfig.h + FreeRTOS.h include surface
)

# bflb_lhal carries bflb_sec_trng.h (the HW TRNG the RNG hooks use), both chips.
target_link_libraries(bouffalo_freertos_tcp PRIVATE bflb_lhal)

# wl80211.h (struct iovec / ALIGN4_HI / WL80211_VIF_* / wl80211_tcpip_input proto)
# transitively pulls "queue/queue.h" + "tree/tree.h" — only the wl80211 chips need it.
# Headers only — the driver symbols resolve at the final image link via bouffalo_sdk's
# --start-group, so do NOT link the wl80211 target (cycle).
if("${BOUFFALO_CHIP}" STREQUAL "bl616" OR "${BOUFFALO_CHIP}" STREQUAL "bl616cl")
    target_link_libraries(bouffalo_freertos_tcp PRIVATE bflb_queue bflb_tree)
    target_include_directories(bouffalo_freertos_tcp PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../wl80211/include")
endif()

target_compile_options(bouffalo_freertos_tcp PRIVATE
    -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-sign-compare)
