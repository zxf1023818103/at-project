# deviceshadow_config — AWS IoT Device Shadow (submodule component/deviceshadow_config/deviceshadow
# @ v1.4.2): assembles/parses the Shadow MQTT topic strings (update / get / delete + accepted /
# rejected / delta) and matches incoming topics. The JSON shadow document itself is handled by
# the consumer (e.g. coreJSON) — this library is topic-string only.
#
# Built via the submodule's OWN upstream CMake (add_subdirectory -> the `aws_device_shadow`
# target, from shadowFilePaths.cmake), like coremqtt_config. Our shadow_config.h (this dir) is
# put on its include path; `deviceshadow_config` is the INTERFACE umbrella consumers link.
# No external library dependency. Cross-platform: no chip gate.
add_subdirectory(deviceshadow "${CMAKE_CURRENT_BINARY_DIR}/upstream" EXCLUDE_FROM_ALL)

# shadow.c pulls shadow_config_defaults.h -> shadow_config.h: put our config header on the
# target's path (PUBLIC). SYSTEM keeps it out of -Werror; -w silences the vendor sources.
target_include_directories(aws_device_shadow SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(aws_device_shadow PRIVATE -w)

# Umbrella INTERFACE (伞形): consumers link `deviceshadow_config` to get Device Shadow.
add_library(deviceshadow_config INTERFACE)
target_link_libraries(deviceshadow_config INTERFACE aws_device_shadow)
