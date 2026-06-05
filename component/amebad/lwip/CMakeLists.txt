# component/amebad/lwip — RESIDUAL netif shim only. lwIP is REMOVED: the IP stack
# is FreeRTOS-Plus-TCP, the submodule component/thirdparty/lwip_amebad is gone, and
# the lwip library build (lwipcore / amebad_lwip / the FreeRTOS port + lwipopts.h /
# lwippools.h) is deleted. All that remains under config/ is the single lwip-free
# vendored header lwip_netconf.h — the minimal `struct netif` (layout-asserted) +
# NET_IF_NUM + LwIP_*/netif_* decls that the prebuilt lib_wlan.a ABI hard-requires.
#
# The INTERFACE target keeps its historical name `amebad_lwip_headers` so consumer
# link edges (wlan/wifi) don't churn; it now exposes only the shim header dir.
# (Dir/target still named "lwip" for low-churn; it's the irreducible driver ABI
# shim, not lwIP.)

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

add_library(amebad_lwip_headers INTERFACE)
target_include_directories(amebad_lwip_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/config")   # lwip_netconf.h (vendored, lwip-free)
