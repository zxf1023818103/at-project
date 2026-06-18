# coremqtt-agent_config — FreeRTOS coreMQTT-Agent (submodule
# component/coremqtt-agent_config/coremqtt-agent). A thread-safe wrapper over coreMQTT: a
# single agent task owns the MQTT connection and serialises API calls from other tasks
# through a command queue (core_mqtt_agent.c + core_mqtt_agent_command_functions.c).
#
# HAND WRAPPER (like corepkcs11_config), NOT the upstream add_subdirectory: coreMQTT-Agent
# ships no CMakeLists.txt of its own — only mqttAgentFilePaths.cmake. We
# `include(mqttAgentFilePaths.cmake)` to reuse the upstream file list (MQTT_AGENT_SOURCES,
# MQTT_AGENT_INCLUDE_PUBLIC_DIRS) and build our own archive.
#
# Depends on coreMQTT: core_mqtt_agent.h includes <core_mqtt.h>/<core_mqtt_state.h> and the
# config defaults include core_mqtt_config_defaults.h — so coremqtt_config (the coreMQTT
# umbrella) is linked PUBLIC for its headers + symbols.
#
# Consumer MUST provide (this lib does not, by design): an MQTTAgentMessageInterface_t —
# the message-queue backend (send / recv / alloc / release of MQTTAgentCommand_t*) that the
# agent task and the calling tasks rendezvous on, typically over a FreeRTOS queue.
#
# Cross-platform: no chip gate. The agent is transport-agnostic (it drives a coreMQTT
# context the consumer wired to a TransportInterface_t), so it has no platform dependency.
#
# Targets produced:
#   coremqtt_agent         STATIC — MQTT_AGENT_SOURCES (EXCLUDE_FROM_ALL).
#   coremqtt-agent_config  INTERFACE umbrella over it.
include("${CMAKE_CURRENT_SOURCE_DIR}/coremqtt-agent/mqttAgentFilePaths.cmake")  # MQTT_AGENT_SOURCES, MQTT_AGENT_INCLUDE_PUBLIC_DIRS

add_library(coremqtt_agent STATIC EXCLUDE_FROM_ALL ${MQTT_AGENT_SOURCES})

# PUBLIC (SYSTEM, to keep the vendor headers out of -Werror): coreMQTT-Agent's own include/
# (from mqttAgentFilePaths.cmake) and our core_mqtt_agent_config.h. core_mqtt_agent.c pulls
# core_mqtt_agent_config_defaults.h -> core_mqtt_agent_config.h (this dir).
target_include_directories(coremqtt_agent SYSTEM PUBLIC
    ${MQTT_AGENT_INCLUDE_PUBLIC_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# coreMQTT backend (headers + symbols; the config defaults chain into core_mqtt_config_defaults.h).
target_link_libraries(coremqtt_agent PUBLIC coremqtt_config)
target_compile_options(coremqtt_agent PRIVATE -w)

# Umbrella INTERFACE (伞形): consumers link `coremqtt-agent_config` to get the MQTT agent.
add_library(coremqtt-agent_config INTERFACE)
target_link_libraries(coremqtt-agent_config INTERFACE coremqtt_agent)
