# component/freertos-plus-tcp_config — FreeRTOS-Plus-TCP V4.0.0 submodule
# (component/freertos-plus-tcp_config/freertos-plus-tcp) built via its OWN upstream CMake
# (add_subdirectory), the ngtcp2_config way. Produces the upstream `freertos_plus_tcp`
# core stack; `freertos_tcp_config` is kept as the consumer-facing INTERFACE umbrella over
# it (existing consumers link that name), and `freertos-plus-tcp_config` is the dir umbrella.
#
# We add_subdirectory the submodule's source/ subdir, NOT its top-level CMakeLists: the
# top-level unconditionally FetchContent-clones FreeRTOS-Kernel + CMock from GitHub and
# add_subdirectory(test)->build-combination (an x86 test-config builder), none of which
# belong in this cross-compile. source/ builds the core + add_subdirectory(portable)
# (BufferAllocation + the GCC compiler port + the board NetworkInterfaces, which self-gate
# on FREERTOS_PLUS_TCP_NETWORK_IF).
#
# Custom NetworkInterface: A_CUSTOM_NETWORK_IF — the real Realtek bridge is
# component/amebad/freertos_tcp_port (amebad_freertos_tcp). Upstream PRIVATE-links
# `freertos_plus_tcp_network_if` (and `_utilities`) into the core; we satisfy those names
# with empty placeholder INTERFACE libs so there is no CMake-level cycle — the real
# NetworkInterface symbols resolve at the app_km4 image link (amebad_freertos_tcp is
# co-listed in app_km4's --start-group with freertos_plus_tcp).
#
# FreeRTOSIPConfig.h (this dir, DHCP-only IPv4) is put on the core's include path; the
# upstream TCP/IPv6/DNS sources compile near-empty with those features off.
#
# KM4-only in practice (added from component/amebad/CMakeLists; Bouffalo uses lwip).

# Upstream options — pin BEFORE add_subdirectory.
set(FREERTOS_PLUS_TCP_BUFFER_ALLOCATION "2" CACHE STRING "" FORCE)   # heap (BufferAllocation_2)
set(FREERTOS_PLUS_TCP_NETWORK_IF "A_CUSTOM_NETWORK_IF" CACHE STRING "" FORCE)
set(FREERTOS_PLUS_TCP_COMPILER "" CACHE STRING "" FORCE)             # auto-detect GNU

# Placeholders for the names the core PRIVATE-links (real netif = amebad_freertos_tcp,
# resolved at image link). Empty INTERFACE -> no archive, no cycle.
add_library(freertos_plus_tcp_network_if INTERFACE)
add_library(freertos_plus_tcp_utilities INTERFACE)

add_subdirectory(freertos-plus-tcp/source "${CMAKE_CURRENT_BINARY_DIR}/upstream" EXCLUDE_FROM_ALL)

# Our FreeRTOSIPConfig.h (DHCP-only IPv4) onto the core; silence the vendor warnings.
target_include_directories(freertos_plus_tcp SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(freertos_plus_tcp PRIVATE -w)
target_compile_options(freertos_plus_tcp_port PRIVATE -w)

# at_netif.c — the cross-platform STA NetworkInterface helper: the descriptor +
# prvInitialise/prvGetPhyLinkStatus/prvOutput, the DHCP IPv4 bring-up, the RX-submit
# tail, and the single-per-image stack callbacks (random / hostname / DNS-query /
# network-event). Shared by the amebad Realtek bridge (amebad_freertos_tcp) and the
# Bouffalo bl602 / wl80211 bridges (bouffalo_freertos_tcp), which thread their WiFi
# driver in via at_netif_ops_t. Compiled INTO the core stack target so it lands in
# every networking image's link path automatically (the stack is already inside each
# image's --start-group, and both bridges link freertos_plus_tcp PUBLIC) — no
# link-group churn. Warnings kept on for our file (the stack itself is built -w).
target_sources(freertos_plus_tcp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/at_netif.c")
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/at_netif.c"
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")

# Umbrella INTERFACE (伞形): header-only consumers (syscall_config) link
# `freertos-plus-tcp_config` to get FreeRTOSIPConfig.h + the FreeRTOS_IP API headers.
# (Archive consumers — the app_km4 link group, the Realtek bridge — link freertos_plus_tcp
# / freertos_plus_tcp_port directly so they stay INSIDE the --start-group with no bridge
# target dragging the group into a cycle.)
add_library(freertos-plus-tcp_config INTERFACE)
target_link_libraries(freertos-plus-tcp_config INTERFACE freertos_plus_tcp)
