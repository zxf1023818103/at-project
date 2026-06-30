# coremqtt_config — FreeRTOS coreMQTT (submodule component/coremqtt_config/coremqtt,
# v5.0.2). Built via the submodule's OWN upstream CMake (add_subdirectory -> the
# `core_mqtt` target), the way ngtcp2_config delegates to ngtcp2_static — not a
# hand-maintained file list. The project's core_mqtt_config.h (this dir) is put on
# core_mqtt's include path; `coremqtt_config` is the INTERFACE umbrella consumers link.
#
# Cross-platform: no chip gate. coreMQTT is transport-agnostic (the consumer supplies the
# TransportInterface_t + MQTTGetCurrentTimeFunc_t), so it has no platform dependency.

# Upstream add_library(core_mqtt) has no explicit type -> follows BUILD_SHARED_LIBS;
# pin it static BEFORE add_subdirectory. EXCLUDE_FROM_ALL: built only when linked.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(coremqtt "${CMAKE_CURRENT_BINARY_DIR}/upstream" EXCLUDE_FROM_ALL)

# core_mqtt.c pulls core_mqtt_config_defaults.h -> core_mqtt_config.h: put our config
# header on the target's path (PUBLIC so consumers of <core_mqtt.h> see it too). SYSTEM
# keeps the header dir out of -Werror; silence the vendor warnings. Touching the upstream
# target here is the same pattern ngtcp2_config uses on ngtcp2_static.
target_include_directories(core_mqtt SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(core_mqtt PRIVATE -w)

# Umbrella INTERFACE (伞形): consumers link `coremqtt_config` to get core_mqtt.
add_library(coremqtt_config INTERFACE)
target_link_libraries(coremqtt_config INTERFACE core_mqtt)
