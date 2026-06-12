# corehttp_config — FreeRTOS coreHTTP (submodule component/corehttp_config/corehttp,
# v3.1.3, with the bundled llhttp parser). Built via the submodule's OWN upstream CMake
# (add_subdirectory -> the `core_http` target, carrying HTTP_SOURCES + llhttp), the way
# ngtcp2_config delegates to the upstream build — not a hand-maintained file list.
# Upstream's core_http is INTERFACE-only (its target_sources propagate to the linker), so
# we bake it into a STATIC `corehttp` archive and add our core_http_config.h; the
# `corehttp_config` INTERFACE is the umbrella consumers link.
#
# Cross-platform: no chip gate. coreHTTP is transport-agnostic (the consumer supplies the
# TransportInterface_t), so it has no platform dependency.

# EXCLUDE_FROM_ALL: built only when linked. Defines core_http (INTERFACE) +
# coremqtt_corehttp_transport_interface (INTERFACE).
add_subdirectory(corehttp "${CMAKE_CURRENT_BINARY_DIR}/upstream" EXCLUDE_FROM_ALL)

# Bake the upstream INTERFACE sources (core_http_client.c + llhttp) into a STATIC archive
# and attach our config header. core_http_client.c pulls core_http_config_defaults.h ->
# core_http_config.h (this dir). PUBLIC so consumers of <core_http_client.h> see it too;
# SYSTEM keeps the header dir out of -Werror; -w silences the vendor sources.
add_library(corehttp STATIC EXCLUDE_FROM_ALL)
target_link_libraries(corehttp PUBLIC core_http)
target_include_directories(corehttp SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(corehttp PRIVATE -w)

# Umbrella INTERFACE (伞形): consumers link `corehttp_config` to get the coreHTTP lib.
add_library(corehttp_config INTERFACE)
target_link_libraries(corehttp_config INTERFACE corehttp)
