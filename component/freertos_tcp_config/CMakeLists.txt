# component/freertos_tcp_config — FreeRTOS-Plus-TCP V4.0.0 config + core stack.
#
# The platform-independent half of the FreeRTOS+TCP integration: our
# FreeRTOSIPConfig.h (flat at the component root, features gated to DHCP-only IPv4 —
# no TCP/IPv6/DNS) plus the whole upstream core stack and a buffer manager, into the
# `freertos_tcp_config` lib. The platform NetworkInterface (the Realtek WiFi
# driver <-> stack bridge) lives in the port (component/amebad/freertos_tcp_port),
# which links this.
#
# Builds: the upstream source set (42 files; TCP/IPv6/DNS compile near-empty with
# the features off) + BufferAllocation_2 (heap). Upstream is the submodule
# component/thirdparty/freertos-plus-tcp.
#
# NB: BufferAllocation_1 (static pool) was tried to cut per-packet pvPortMalloc
# but its ~61 KB BSS overran the SRAM the closed lib_wlan.a hardcodes as free
# (link didn't catch it — driver RAM is invisible to our ld), faulting the driver
# at wifi_connect. And the per-packet cost is ~0.5-0.8 ms (closed-driver path),
# not malloc (µs), so it wouldn't have helped. Staying on heap (_2).
#
# Cross-platform: no chip gate — the portable FreeRTOS+TCP stack builds wherever
# freertos_tcp_config is linked (EXCLUDE_FROM_ALL until then). The `nimble`-style
# split: portable stack here, platform bridge in the port. It needs the kernel +
# FreeRTOSConfig on whatever platform builds it.

set(FT "${CMAKE_SOURCE_DIR}/component/thirdparty/freertos-plus-tcp")

# Upstream library sources — glob the stack. Globbing the UPSTREAM tree is fine;
# the platform glue (the Realtek NetworkInterface) is listed explicitly in the port.
file(GLOB _ft_src CONFIGURE_DEPENDS "${FT}/source/*.c")

add_library(freertos_tcp_config STATIC EXCLUDE_FROM_ALL
    ${_ft_src}
    "${FT}/source/portable/BufferManagement/BufferAllocation_2.c"
)

# Config + header surface. PUBLIC so both this lib's own sources and the port (and
# header-only consumers: lfs_port, syscalls) see FreeRTOSIPConfig.h + the stack API.
target_include_directories(freertos_tcp_config PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"                  # FreeRTOSIPConfig.h (flat)
    "${FT}/source/include"
    "${FT}/source/portable/Compiler/GCC"          # pack_struct_start/end.h
)

# Just the kernel: FreeRTOS carries FreeRTOSConfig.h (configMAX_PRIORITIES etc.) and,
# transitively, whatever the platform's FreeRTOSConfig.h needs — on AmebaD that's
# platform_autoconf.h, which reaches here via FreeRTOS -> freertos_config ->
# amebad_sdk_includes (wired on the AmebaD side). Naming no platform target keeps
# this stack reusable as-is by any FreeRTOS platform (Dplus / Green2 / Bouffalo).
target_link_libraries(freertos_tcp_config PUBLIC
    FreeRTOS
)

target_compile_options(freertos_tcp_config PRIVATE
    -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
    -Wno-sign-compare)
