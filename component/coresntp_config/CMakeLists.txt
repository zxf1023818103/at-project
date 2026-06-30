# coresntp_config — FreeRTOS coreSNTP (submodule component/coresntp_config/coresntp,
# the SNTPv4 client: core_sntp_client.c + core_sntp_serializer.c). Built via the
# submodule's OWN upstream CMake (add_subdirectory -> the `core_sntp` target), the way
# ngtcp2_config / corehttp_config delegate to the upstream build — not a hand-maintained
# file list. Upstream's core_sntp is INTERFACE-only (its target_sources propagate to the
# linker), so we bake it into a STATIC `coresntp` archive and add our core_sntp_config.h;
# the `coresntp_config` INTERFACE is the umbrella consumers link.
#
# Cross-platform: no chip gate. coreSNTP is transport-agnostic — the consumer supplies the
# UDP transport (UdpTransportInterface_t), the time provider (SntpGetTime_t/SntpSetTime_t)
# and, for authenticated NTP, the auth interface — so it has no platform dependency.

# EXCLUDE_FROM_ALL: built only when linked. Defines core_sntp (INTERFACE).
add_subdirectory(coresntp "${CMAKE_CURRENT_BINARY_DIR}/upstream" EXCLUDE_FROM_ALL)

# Bake the upstream INTERFACE sources (core_sntp_client.c + core_sntp_serializer.c) into a
# STATIC archive and attach our config header. core_sntp_client.c pulls
# core_sntp_config_defaults.h -> core_sntp_config.h (this dir). PUBLIC so consumers of
# <core_sntp_client.h> see it too; SYSTEM keeps the header dir out of -Werror; -w silences
# the vendor sources.
add_library(coresntp STATIC EXCLUDE_FROM_ALL)
target_link_libraries(coresntp PUBLIC core_sntp)
target_include_directories(coresntp SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(coresntp PRIVATE -w)

# Umbrella INTERFACE (伞形): consumers link `coresntp_config` to get the coreSNTP client.
add_library(coresntp_config INTERFACE)
target_link_libraries(coresntp_config INTERFACE coresntp)
