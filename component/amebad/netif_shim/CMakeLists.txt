# component/amebad/netif_shim — the residual driver<->netif ABI shim. lwIP is
# REMOVED (the IP stack is FreeRTOS-Plus-TCP; the component/thirdparty/lwip_amebad
# submodule + the lwip library/port/config are gone). All that remains is the
# single lwip-free vendored header netif_shim.h: the minimal `struct netif`
# (layout-asserted: hwaddr@+50 / hwaddr_len@+56, 72-byte stride) + NET_IF_NUM +
# the LwIP_*/netif_* decls that the prebuilt lib_wlan.a ABI hard-requires
# (LwIP_GetMAC is load-bearing on the connect-path mgmt-frame filter).
#
# Exposed via the INTERFACE target amebad_netif_shim (was amebad_lwip_headers).

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

add_library(amebad_netif_shim INTERFACE)
target_include_directories(amebad_netif_shim SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}")   # netif_shim.h
