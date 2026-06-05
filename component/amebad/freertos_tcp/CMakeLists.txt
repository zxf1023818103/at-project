# amebad freertos_tcp — FreeRTOS-Plus-TCP V4.0.0 (component/thirdparty/
# freertos-plus-tcp) for KM4, wired to the Realtek WiFi driver. DHCP-only IPv4
# bring-up: associate + get an IP. Replaces lwip's IP role for this build
# (lwip's ethernetif_recv is overridden here; the lwip IP stack is not linked).
#
# Builds: the whole FreeRTOS+TCP source set (features gated off in
# config/FreeRTOSIPConfig.h — no TCP/IPv6/DNS) + BufferAllocation_2 (heap) + our
# Realtek NetworkInterface (realtek_netif.c: TX/RX bridge + app hooks) + the
# xnetif[] storage the vendored lwip_intf.c needs (realtek_xnetif.c).
#
# KM4 only. EXCLUDE_FROM_ALL until app_km4 links it (build with
# `cmake --build <dir> -t amebad_freertos_tcp`).

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

set(FT "${CMAKE_SOURCE_DIR}/component/thirdparty/freertos-plus-tcp")

# Upstream library sources — glob the stack (42 files; TCP/IPv6/DNS compile to
# near-empty with the features gated off). Globbing the UPSTREAM tree is fine;
# our own glue below is listed explicitly.
file(GLOB _ft_src CONFIGURE_DEPENDS "${FT}/source/*.c")

add_library(amebad_freertos_tcp STATIC EXCLUDE_FROM_ALL
    ${_ft_src}
    "${FT}/source/portable/BufferManagement/BufferAllocation_2.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/realtek_netif.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/realtek_xnetif.c"
)

target_include_directories(amebad_freertos_tcp PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/config"          # FreeRTOSIPConfig.h
    "${FT}/source/include"
    "${FT}/source/portable/Compiler/GCC"          # pack_struct_start/end.h
)
# realtek_xnetif.c needs lwip's struct netif + NET_IF_NUM (the one lwip-typed
# dependency); pull the lwip headers (NOT the lwip lib).
target_include_directories(amebad_freertos_tcp PRIVATE
    "${CMAKE_SOURCE_DIR}/component/thirdparty/lwip_amebad/src/include"
    "${CMAKE_SOURCE_DIR}/component/thirdparty/lwip_amebad/src/include/lwip"
    # lwip's arch.h does `#include "arch/cc.h"` -> port/realtek/arch/cc.h, so the
    # PARENT of arch/ (port/realtek) goes on the path (same trick as amebad/lwip).
    "${CMAKE_SOURCE_DIR}/component/thirdparty/lwip_amebad/port/realtek"
    "${CMAKE_SOURCE_DIR}/component/amebad/lwip/config"
    # wlan driver osdep headers (lwip_intf.h structs / rltk_* decls) — headers
    # only; the actual symbols resolve at the exe link via the cross_refs group,
    # so we must NOT *link* amebad_wlan here (it's a group member -> cycle).
    "${CMAKE_SOURCE_DIR}/component/amebad/wlan/osdep"
)

target_link_libraries(amebad_freertos_tcp PUBLIC
    FreeRTOS               # kernel + FreeRTOSConfig (configMAX_PRIORITIES etc.)
    amebad_sdk_includes
)

target_compile_options(amebad_freertos_tcp PRIVATE
    -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
    -Wno-sign-compare)
