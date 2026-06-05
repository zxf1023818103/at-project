# amebad lwip — build lwip from the SHARED submodule component/thirdparty/lwip_amebad
# (zxf1023818103/lwip @ ameba-d-port = lwip v2.2.0 + Realtek patches + an in-tree
# port/realtek/ adapter), instead of compiling the SDK's lwip in-place. Same model
# as FreeRTOS / mbedtls: clean(-ish) upstream source in component/thirdparty/, a
# per-platform config + port here.
#
# (lwip_amebad is the AmebaD staging branch; once its patches are reconciled into
# the single shared component/thirdparty/lwip — see the branch-merge eval — this
# points there and lwip_amebad is dropped, like freertos_amebad was.)
#
# This slice builds lwipcore (the platform-clean TCP/IP core) + the FreeRTOS
# sys_arch port. ethernetif (the netif driver) is NOT built yet — it needs the
# Realtek WiFi driver, which isn't wired; it's only required at netif bring-up, so
# lwipcore + sys_arch archive fine without it. KM4 only (TCP/IP is HP-side).

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

set(LWIP_DIR "${CMAKE_SOURCE_DIR}/component/thirdparty/lwip_amebad")

# Include dirs Filelists.cmake injects into lwipcore (PRIVATE) at definition:
#   config/                    our lwipopts.h
#   src/include                lwip's own headers
#   port/realtek               so lwip's `#include "arch/cc.h"` / "arch/sys_arch.h"
#                              resolve to port/realtek/arch/*
#   port/realtek/freertos      the FreeRTOS sys_arch.h
set(LWIP_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/config"
    "${LWIP_DIR}/src/include"
    # Realtek port files (ethernetif.c / lwip_intf.c) use FLAT lwip includes
    # ("err.h", not "lwip/err.h"), so the lwip header dir itself is on the path.
    "${LWIP_DIR}/src/include/lwip"
    "${LWIP_DIR}/port/realtek"
    "${LWIP_DIR}/port/realtek/freertos"
)

# Header-only INTERFACE: the lwip + arch + config include dirs, WITHOUT the lwip
# library. Consumers that only need lwip headers at compile time (the wlan driver
# glue, the wifi api, the FreeRTOS+TCP bridge's xnetif) link this instead of
# amebad_lwip — linking the lib would drag lwipcore into their dependency closure
# and form a cycle with the app's cross_refs link group (and, for the
# FreeRTOS+TCP build, pull in the unused lwip IP stack).
add_library(amebad_lwip_headers INTERFACE)
target_include_directories(amebad_lwip_headers SYSTEM INTERFACE
    "${LWIP_DIR}/port/realtek/freertos"        # ethernetif.h
    ${LWIP_INCLUDE_DIRS})
# Pure include dirs — NO library/FreeRTOS link (those would re-enter the app's
# cross_refs group and form a cycle). Consumers link FreeRTOS/amebad_sdk_includes
# themselves, which is what resolves lwipopts.h's platform + sys.h headers.

# Defines lwipcore (EXCLUDE_FROM_ALL) from lwipnoapps_SRCS, generates lwip/init.h.
include("${LWIP_DIR}/src/Filelists.cmake")

# lwipopts.h pulls SDK platform headers (platform_opts.h, platform/platform_stdlib.h,
# autoconf.h) via amebad_sdk_includes; sys.h/sys_arch needs FreeRTOS. Expose the
# lwip + arch + config headers PUBLIC so consumers (and the port below) resolve
# lwipopts.h / cc.h / lwip/*.h.
target_link_libraries(lwipcore PUBLIC amebad_sdk_includes FreeRTOS)
target_include_directories(lwipcore PUBLIC ${LWIP_INCLUDE_DIRS})
target_compile_options(lwipcore PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)

# Port: FreeRTOS sys_arch (mutex/sem/mbox/thread over FreeRTOS) + ethernetif (the
# netif <-> WiFi driver bridge: TX via rltk_wlan_send, RX hand-off to tcpip_input).
# ethernetif.c reaches into the driver's osdep headers (skbuff/wlan_intf) and the
# wifi API (wifi_constants), so it links amebad_wlan (header surface) + the wifi
# header interface. Consumers linking amebad_lwip get the whole TCP/IP stack +
# netif + headers.
add_library(amebad_lwip STATIC
    "${LWIP_DIR}/port/realtek/freertos/sys_arch.c"
    "${LWIP_DIR}/port/realtek/freertos/ethernetif.c"
)
target_link_libraries(amebad_lwip PUBLIC
    lwipcore
    amebad_sdk_includes
    amebad_wlan              # driver osdep headers (skbuff.h / wlan_intf.h)
    amebad_wifi_headers      # <wifi/wifi_conf.h> / wifi_constants.h
    FreeRTOS
)
# ethernetif.h lives in the lwip realtek port dir; expose it so the driver glue +
# lwip_netconf's `#include <ethernetif.h>` resolves.
target_include_directories(amebad_lwip PUBLIC
    "${LWIP_DIR}/port/realtek/freertos")
target_compile_options(amebad_lwip PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)

# Nothing links amebad_lwip yet (sockets / net_sockets not wired), so keep it out
# of the default build; a consumer linking it pulls it in, or build explicitly:
#   cmake --build <dir> -t amebad_lwip
set_target_properties(amebad_lwip PROPERTIES EXCLUDE_FROM_ALL TRUE)
