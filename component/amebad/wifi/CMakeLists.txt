# amebad wifi — host WiFi API + network bring-up layer (source). Sits on top of
# the prebuilt driver (component/amebad/wlan: lib_wlan.a/lib_wps.a + the source
# glue amebad_wlan_glue) and provides the user-facing entry points:
#   wifi/wifi_conf.c   wifi_on / wifi_off / wifi_scan_networks / wifi_connect ...
#   wifi/wifi_util.c   ioctl/iwpriv helpers the API is built on
#   wifi/wifi_ind.c    event indication dispatch (driver -> app callbacks)
#   network/wlan_network.c   wlan_network() bring-up entry (LwIP_Init + wifi_on)
#   network/lwip_netconf.c   LwIP_Init / netif setup (registers ethernetif)
#
# Layout: all api/wifi + api/network files sit flat in this dir. Rooted includes
# the WiFi sources use (`<wifi/wifi_conf.h>`, `<at_cmd/atcmd_wifi.h>`) resolve by
# exposing component/amebad as a root — `wifi/` and `at_cmd/` are real sibling
# component dirs under it. Flat includes (`"wifi_conf.h"`, `"main.h"`) resolve
# via this dir. Network-service / example / mbed rooted headers (`<dhcp/...>`,
# `<wlan_fast_connect/...>`, spdio_api.h) come from the sdk_headers mirror roots.
#
# KM4-only. EXCLUDE_FROM_ALL until app_km4 links it (build explicitly with
# `cmake --build <dir> -t amebad_wifi`). Resync from component/common/api/{wifi,
# network} when the platform/ameba-rtos-d pointer moves.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

# Header surface, shared with consumers that only need the API decls (e.g. the
# wlan driver glue's `#include <wifi/wifi_util.h>`, ethernetif). GLOBAL so any
# sibling can pick it up without a link edge.
set(_sdk_mir "${CMAKE_SOURCE_DIR}/component/amebad/sdk_headers/component/common")
add_library(amebad_wifi_headers INTERFACE)
target_include_directories(amebad_wifi_headers SYSTEM INTERFACE
    "${CMAKE_SOURCE_DIR}/component/amebad"  # -> <wifi/wifi_conf.h>, <at_cmd/atcmd_wifi.h>
    "${CMAKE_CURRENT_SOURCE_DIR}"           # -> flat "wifi_conf.h" / "main.h" / "netconf.h"
    "${_sdk_mir}/network"                   # -> <dhcp/dhcps.h>
    "${_sdk_mir}/example"                   # -> <wlan_fast_connect/example_wlan_fast_connect.h>
    "${_sdk_mir}/mbed/hal_ext"              # -> spdio_api.h / flash_api.h
    "${_sdk_mir}/mbed/hal"                  # -> pinmap.h (pulled by flash_api.h)
    "${_sdk_mir}/mbed/targets/hal/rtl8721d" # -> device.h / objects.h / PinNames.h
)

add_library(amebad_wifi STATIC EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_SOURCE_DIR}/wifi_conf.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/wifi_util.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/wifi_ind.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/wlan_network.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lwip_netconf.c"
)

target_link_libraries(amebad_wifi PUBLIC
    amebad_wifi_headers
    amebad_sdk_includes
    amebad_wlan              # wlan public + osdep/rom headers
    amebad_osdep
    amebad_lwip              # lwipcore + sys_arch + ethernetif
    amebad_wpa_supplicant    # supplicant config headers (connect path)
    FreeRTOS
)

# Vendor code — quiet the warnings we don't own (same set as the other vendored
# components). -Wno-error too in case the tree is built with -Werror.
target_compile_options(amebad_wifi PRIVATE
    -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
    -Wno-sign-compare -Wno-unused-but-set-variable)
