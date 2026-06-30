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

# Upstream lwIP 2.1.2 (component/lwip_config/lwip) is built by component/lwip_config
# itself — it reads this port's include surface (lwipopts.h + arch/) and consumes lwip's
# src/Filelists.cmake to produce lwipcore. Added here, AFTER bflb_lwip_port is defined, so
# its INTERFACE_INCLUDE_DIRECTORIES is available; out-of-tree add needs an explicit binary
# dir. (lwip is NOT add_subdirectory'd: its top-level CMakeLists is Bouffalo sdk_*-macro
# style, unavailable in this clean build — see component/lwip_config.)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../lwip_config"
                 "${CMAKE_BINARY_DIR}/component/lwip_config")
