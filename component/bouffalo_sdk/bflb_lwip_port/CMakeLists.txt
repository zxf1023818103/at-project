# bflb_lwip_port — Bouffalo's lwip port layer (arch headers + lwipopts.h +
# ethernetif + sys_arch). Extracted from
# platform/bouffalo_sdk/components/net/lwip/lwip/lwip-port/ so it lives
# outside the upstream lwip submodule.
#
# Layout:
#   arch/         lwip arch abstraction headers (cc.h, perf.h, sys_arch.h, …)
#   config/       lwipopts.h — user-config consumed by every lwip TU
#   FreeRTOS/     sys_arch.c + ethernetif.c for the FreeRTOS backend
#   Standalone/   ethernetif.c for the bare-metal backend
#
# Default backend is FreeRTOS (the rest of bouffalo_sdk assumes RTOS).
# Flip BFLB_LWIP_PORT_STANDALONE=ON to swap.

option(BFLB_LWIP_PORT_STANDALONE "Build the bare-metal port instead of FreeRTOS" OFF)

add_library(bflb_lwip_port STATIC)

# arch/ + config/ go on PUBLIC includes so consumers (and lwip itself once
# it links against this) see lwipopts.h, cc.h, perf.h, sys_arch.h.
target_include_directories(bflb_lwip_port PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/config"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(BFLB_LWIP_PORT_STANDALONE)
    target_sources(bflb_lwip_port PRIVATE
        Standalone/ethernetif.c
    )
    target_include_directories(bflb_lwip_port PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Standalone"
    )
else()
    target_sources(bflb_lwip_port PRIVATE
        FreeRTOS/sys_arch.c
        FreeRTOS/ethernetif.c
    )
    target_include_directories(bflb_lwip_port PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/FreeRTOS"
    )
endif()
