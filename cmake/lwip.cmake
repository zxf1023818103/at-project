include_guard()

# Wires component/lwip (Bouffalo-port of lwIP 2.1.2) into the chip-
# toolchain build. Bouffalo's patches replaced the upstream top-level
# CMakeLists with their sdk_* macro style, so we bypass it and consume
# src/Filelists.cmake directly — that's upstream's source-list
# distribution file, untouched by Bouffalo's patches.
#
# Targets produced (both STATIC, EXCLUDE_FROM_ALL — only linked when a
# consumer references them):
#   lwipcore       core stack (IPv4 + IPv6 + netif + api + sys + tcp/udp)
#   lwipallapps    bundled apps (HTTP, MDNS, SNTP, etc.) — optional
#
# Required environment at include time:
#   AT_PROJECT_ROOT must already be set.
#   bflb_lwip_port must already be add_subdirectory'd (it carries the
#     lwipopts.h + arch headers that lwipcore's sources #include).

if(NOT DEFINED AT_PROJECT_ROOT)
    message(FATAL_ERROR "AT_PROJECT_ROOT must be set before including cmake/lwip.cmake")
endif()

if(NOT TARGET bflb_lwip_port)
    message(FATAL_ERROR
        "cmake/lwip.cmake: bflb_lwip_port target is missing. "
        "Include component/bouffalo_sdk before lwip.cmake — the port "
        "layer carries lwipopts.h and the arch headers lwip needs.")
endif()

# Filelists wants LWIP_DIR pointing at the submodule root.
set(LWIP_DIR "${AT_PROJECT_ROOT}/component/lwip")

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
        # And bring the port headers in transitively.
        target_link_libraries(${_t} PUBLIC bflb_lwip_port)
    endif()
endforeach()
