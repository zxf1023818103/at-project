# component/amebad/freertos_tcp_port — Realtek NetworkInterface for FreeRTOS+TCP.
#
# The AmebaD-specific half of the FreeRTOS+TCP integration: the Realtek WiFi driver
# <-> stack bridge, compiled into the `amebad_freertos_tcp` lib (name kept so the
# consumers — lfs_port, syscalls, app_km4's link group — don't churn). The core
# stack + config (FreeRTOSIPConfig.h) come from component/freertos_plus_tcp,
# linked PUBLIC below so header-only consumers reach FreeRTOS_IP.h/FreeRTOS_Sockets.h
# transitively.
#
# DHCP-only IPv4 bring-up: associate + get an IP. Replaces lwip's IP role for this
# build (lwip's ethernetif_recv is overridden here; the lwip IP stack is not linked).
#   - realtek_netif.c : TX/RX bridge + app hooks (amebad_freertos_tcp_set_ready)
#   - realtek_xnetif.c: the xnetif[] storage the vendored lwip_intf.c needs
#
# The stack (freertos_plus_tcp) and this bridge call into each other (stack ->
# NetworkInterface fns here; here -> FreeRTOS_IP APIs), so app_km4 lists BOTH inside
# one --start-group (its cross_refs LINK_GROUP).
#
# KM4 only. EXCLUDE_FROM_ALL until app_km4 links it.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

# netif_shim — the residual driver<->netif ABI shim header (lwIP removed). It now
# lives in this component (its sole structural consumer is the Realtek bridge
# below), but the prebuilt lib_wlan.a ABI surface is pulled by many TUs, so it's
# exposed via the INTERFACE target amebad_netif_shim — the WLAN / WiFi / at_cmd /
# supplicant consumers keep linking that name unchanged.
#
# netif_shim.h: the minimal lwip-free `struct netif` (layout-asserted: hwaddr@+50 /
# hwaddr_len@+56, 72-byte stride) + NET_IF_NUM + the LwIP_*/netif_* decls the
# prebuilt lib_wlan.a ABI hard-requires (LwIP_GetMAC is load-bearing on the
# connect-path mgmt-frame filter).
add_library(amebad_netif_shim INTERFACE)
target_include_directories(amebad_netif_shim SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(amebad_freertos_tcp STATIC EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_SOURCE_DIR}/realtek_netif.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/realtek_xnetif.c"
)

# Core stack + config + the upstream include surface.
target_link_libraries(amebad_freertos_tcp PUBLIC
    freertos_plus_tcp    # FreeRTOSIPConfig.h + the FreeRTOS+TCP core stack/API
    FreeRTOS
    amebad_sdk_includes
)

# realtek_xnetif.c needs netif_shim.h (amebad_netif_shim, this dir) and the wlan
# driver osdep headers (lwip_intf.h structs / rltk_* decls — headers only; symbols
# resolve at the exe link via the cross_refs group, so do NOT *link* amebad_wlan
# here -> cycle). lwip is gone.
target_link_libraries(amebad_freertos_tcp PRIVATE amebad_netif_shim)
target_include_directories(amebad_freertos_tcp PRIVATE
    "${CMAKE_SOURCE_DIR}/component/amebad/wlan/osdep"
)

target_compile_options(amebad_freertos_tcp PRIVATE
    -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
    -Wno-sign-compare)
