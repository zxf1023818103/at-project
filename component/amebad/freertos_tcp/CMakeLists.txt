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
# NB: BufferAllocation_1 (static pool) was tried to cut per-packet pvPortMalloc
# but its ~61 KB BSS overran SRAM the closed lib_wlan.a hardcodes as free (link
# didn't catch it — driver RAM is invisible to our ld), faulting the driver at
# wifi_connect. And the per-packet cost is ~0.5-0.8 ms (closed-driver path), not
# malloc (µs), so it wouldn't have helped. Staying on heap (_2).
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
# realtek_xnetif.c needs the netif shim (netif_shim.h: vendored struct netif +
# NET_IF_NUM) and the wlan driver osdep headers (lwip_intf.h structs / rltk_*
# decls — headers only; symbols resolve at the exe link via the cross_refs group,
# so do NOT *link* amebad_wlan here -> cycle). lwip is gone.
target_include_directories(amebad_freertos_tcp PRIVATE
    "${CMAKE_SOURCE_DIR}/component/amebad/netif_shim"      # netif_shim.h (vendored shim)
    "${CMAKE_SOURCE_DIR}/component/amebad/wlan/osdep"
)

target_link_libraries(amebad_freertos_tcp PUBLIC
    FreeRTOS               # kernel + FreeRTOSConfig (configMAX_PRIORITIES etc.)
    amebad_sdk_includes
)

target_compile_options(amebad_freertos_tcp PRIVATE
    -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
    -Wno-sign-compare)
