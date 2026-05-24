# lwip_dhcpd — Bouffalo's raw-DHCP-server-on-lwip app. Extracted from
# platform/bouffalo_sdk/components/net/lwip/lwip/src/apps/dhcpd/ so it sits
# alongside the upstream lwip submodule instead of polluting it.
#
# Deps (consumer must arrange to be on this lib's link line):
#   lwip kernel  — for lwip/{ip,dhcp,inet_chksum,init,netifapi,opt,prot/dhcp,
#                  sockets,tcpip}.h and netif/etharp.h
#   FreeRTOS     — for FreeRTOS.h

add_library(lwip_dhcpd STATIC
    dhcp_server_raw.c
)

target_include_directories(lwip_dhcpd PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Real-target deps — wire them directly now that both are extracted into
# the component/ tree as proper CMake libraries.
target_link_libraries(lwip_dhcpd PUBLIC
    lwipcore
    freertos_kernel
)
