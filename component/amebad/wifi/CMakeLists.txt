# amebad wifi — host WiFi API + network bring-up layer (source). Sits on top of
# the prebuilt driver (component/amebad/wlan: lib_wlan.a/lib_wps.a + the source
# glue amebad_wlan_glue) and provides the user-facing entry points:
#   wifi/wifi_conf.c   wifi_on / wifi_off / wifi_scan_networks / wifi_connect ...
#   wifi/wifi_util.c   ioctl/iwpriv helpers the API is built on
#   wifi/wifi_ind.c    event indication dispatch (driver -> app callbacks)
#   network/wlan_network.c   wlan_network() bring-up entry (compiled but unused;
#                            its LwIP_Init() call is gc-section'd away — no caller)
#
# Layout: all api/wifi + api/network files sit flat in this dir. Rooted includes
# the WiFi sources use (`<wifi/wifi_conf.h>`, `<at_cmd/atcmd_wifi.h>`) resolve by
# exposing component/amebad as a root — `wifi/` and `at_cmd/` are real sibling
# component dirs under it. Flat includes (`"wifi_conf.h"`, `"main.h"`) resolve
# via this dir. Example rooted headers (`<wlan_fast_connect/...>`) come from the
# sdk_headers mirror root; the mbed HAL headers from the mbed component.
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
add_library(amebad_wifi_headers INTERFACE)
target_include_directories(amebad_wifi_headers SYSTEM INTERFACE
    "${CMAKE_SOURCE_DIR}/component/amebad"  # -> <wifi/wifi_conf.h>, <at_cmd/atcmd_wifi.h>
    "${CMAKE_CURRENT_SOURCE_DIR}"           # -> flat "wifi_conf.h" / "main.h" / "netconf.h"
    # mbed HAL (flattened): spdio_api.h / flash_api.h / pinmap.h / device.h /
    # objects.h / PinNames.h — now one flat dir in the mbed component.
    "${CMAKE_SOURCE_DIR}/component/amebad/mbed"
)

add_library(amebad_wifi STATIC EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_SOURCE_DIR}/wifi_conf.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/wifi_util.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/wifi_ind.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/wlan_network.c"
    # NB: lwip_netconf.c (LwIP_Init / xnetif / lwip DHCP) was deleted — the
    # FreeRTOS+TCP bring-up replaces lwip's IP role. xnetif[] is provided by
    # component/amebad/freertos_tcp/realtek_xnetif.c, and wifi_conf.c's residual
    # lwip references (netif_add / dhcp_* etc.) are weak-stubbed in app_km4
    # (freertos_tcp_compat.c) — not on the connect path we use.
)

target_link_libraries(amebad_wifi PUBLIC
    amebad_wifi_headers
    amebad_sdk_includes
    amebad_wlan              # wlan public + osdep/rom headers
    amebad_osdep
    amebad_mbed              # flash_api.c: flash_stream_read (wifi_conf restore_wifi_info_to_flash)
    amebad_netif_shim        # vendored struct netif + NET_IF_NUM (lwip removed)
    amebad_wpa_supplicant    # supplicant config headers (connect path)
    FreeRTOS
)

# Vendor code — quiet the warnings we don't own. -fcommon: the legacy Realtek
# headers declare globals (uart_buf, ...) without `extern`, so every TU that
# includes them emits a tentative definition. The SDK builds with the old GCC
# default (-fcommon, which merges them); GCC 10 defaults to -fno-common (each
# becomes a strong def -> "multiple definition"), so restore -fcommon here.
target_compile_options(amebad_wifi PRIVATE
    -fcommon
    -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
    -Wno-sign-compare -Wno-unused-but-set-variable)
