# devicedefender_config — AWS IoT Device Defender (submodule
# component/devicedefender_config/devicedefender @ v1.4.1): builds the Device Defender MQTT topic
# strings (the metrics-report publish topics + accepted/rejected response topics). The metrics
# report payload (JSON or CBOR) is assembled by the consumer — this library is topic-string only.
#
# Built via the submodule's OWN upstream CMake (add_subdirectory -> the `aws_device_defender`
# target, from defenderFilePaths.cmake), like coremqtt_config. Our defender_config.h (this dir)
# is put on its include path; `devicedefender_config` is the INTERFACE umbrella consumers link.
# No external library dependency. Cross-platform: no chip gate.
add_subdirectory(devicedefender "${CMAKE_CURRENT_BINARY_DIR}/upstream" EXCLUDE_FROM_ALL)

# defender.c pulls defender_config_defaults.h -> defender_config.h: put our config header on the
# target's path (PUBLIC). SYSTEM keeps it out of -Werror; -w silences the vendor sources.
target_include_directories(aws_device_defender SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(aws_device_defender PRIVATE -w)

# Umbrella INTERFACE (伞形): consumers link `devicedefender_config` to get Device Defender.
add_library(devicedefender_config INTERFACE)
target_link_libraries(devicedefender_config INTERFACE aws_device_defender)
