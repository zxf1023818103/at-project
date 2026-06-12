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
    # sys_arch.c + ethernetif.c #include FreeRTOS + lwip headers.
    # FreeRTOS is a real target we can link straight. lwip is the
    # awkward one: the upstream-lwip block below makes lwipcore
    # PUBLIC-link bflb_lwip_port (so its lwipopts.h gets seen during
    # lwipcore's own compile), and a reciprocal target_link_libraries
    # here would close the cycle and break cmake configure. Use a
    # generator expression that pulls lwipcore's
    # INTERFACE_INCLUDE_DIRECTORIES without creating a link edge — same
    # headers, no cycle.
    target_link_libraries(bflb_lwip_port PUBLIC freertos_kernel)
    target_include_directories(bflb_lwip_port PUBLIC
        "$<TARGET_PROPERTY:lwipcore,INTERFACE_INCLUDE_DIRECTORIES>"
    )
endif()

# ---------------------------------------------------------------------
# Upstream lwIP 2.1.2 (component/lwip_config/lwip, Bouffalo-port branch).
# Lives next to the port adapter because the upstream sources are
# unbuildable without bflb_lwip_port's lwipopts.h + arch/ headers, and
# nothing else in the tree consumes upstream lwip directly. Keeping the
# pull here makes the dependency one-directional — thirdparty/ knows
# nothing about Bouffalo.
#
# Bouffalo's patches replaced the upstream top-level CMakeLists with
# their sdk_* macro style, so we bypass it and consume src/Filelists.cmake
# directly — that's upstream's source-list distribution file, untouched
# by Bouffalo's patches.
#
# Targets produced (both STATIC, EXCLUDE_FROM_ALL — only linked when a
# consumer references them):
#   lwipcore       core stack (IPv4 + IPv6 + netif + api + sys + tcp/udp)
#   lwipallapps    bundled apps (HTTP, MDNS, SNTP, etc.) — optional
# ---------------------------------------------------------------------

set(LWIP_DIR "${CMAKE_CURRENT_LIST_DIR}/../../lwip_config/lwip")

# bflb_lwip_port::INTERFACE_INCLUDE_DIRECTORIES gives us lwipopts.h + arch.
get_target_property(_bflb_lwip_port_inc bflb_lwip_port INTERFACE_INCLUDE_DIRECTORIES)

set(LWIP_INCLUDE_DIRS
    "${LWIP_DIR}/src/include"
    ${_bflb_lwip_port_inc}
)
set(LWIP_DEFINITIONS "")
set(LWIP_MBEDTLS_INCLUDE_DIRS "")
set(LWIP_MBEDTLS_DEFINITIONS  "")

include("${LWIP_DIR}/src/Filelists.cmake")

# Filelists adds includes PRIVATE — fine for compiling lwipcore itself,
# but leaves no way for consumers to reach <lwip/...> headers. Promote
# the public lwip include dirs to INTERFACE on both targets so anything
# linking lwipcore / lwipallapps automatically sees them.
foreach(_t lwipcore lwipcore4 lwipcore6 lwipallapps)
    if(TARGET ${_t})
        target_include_directories(${_t} PUBLIC
            "${LWIP_DIR}/src/include"
            "${LWIP_DIR}/src/include/compat/posix"
        )
        target_link_libraries(${_t} PUBLIC bflb_lwip_port)
    endif()
endforeach()
