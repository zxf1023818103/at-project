# component/lwip_config — builds upstream lwIP 2.1.2 (component/lwip_config/lwip,
# Bouffalo-port branch).
#
# lwip is built via its idiomatic src/Filelists.cmake (the upstream source-list
# distribution file), NOT add_subdirectory: Bouffalo's port branch replaced lwip's
# top-level CMakeLists with the SDK's sdk_generate_library() macro style, which the
# project's clean component build doesn't load — so add_subdirectory(lwip) would fail on
# the undefined macro. Filelists.cmake is untouched by those patches and produces the
# canonical `lwipcore` targets.
#
# Consumer-coupled: the upstream sources are unbuildable without the port adapter's
# lwipopts.h + arch/ headers (component/bouffalo_sdk/bflb_lwip_port), and only Bouffalo
# consumes upstream lwip. So this is add_subdirectory'd by bflb_lwip_port AFTER that
# target is defined; we read its include surface for LWIP_INCLUDE_DIRS. Guarded on the
# port target so the dir is inert anywhere else.
#
# Targets produced (STATIC, EXCLUDE_FROM_ALL — only linked when a consumer references):
#   lwipcore       core stack (IPv4 + IPv6 + netif + api + sys + tcp/udp)
#   lwipallapps    bundled apps (HTTP, MDNS, SNTP, …) — optional
if(TARGET bflb_lwip_port)
    set(LWIP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lwip")

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

    # Filelists adds includes PRIVATE — fine for compiling lwipcore itself, but leaves no
    # way for consumers to reach <lwip/...> headers. Promote the public lwip include dirs
    # to INTERFACE on both targets, and link the port back (its lwipopts.h is needed
    # during lwipcore's own compile).
    foreach(_t lwipcore lwipcore4 lwipcore6 lwipallapps)
        if(TARGET ${_t})
            target_include_directories(${_t} PUBLIC
                "${LWIP_DIR}/src/include"
                "${LWIP_DIR}/src/include/compat/posix"
            )
            target_link_libraries(${_t} PUBLIC bflb_lwip_port)
        endif()
    endforeach()
endif()
