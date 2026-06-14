# ota_config — AWS IoT Over-the-Air Update (submodule component/ota_config/ota @ v3.3.0): the OTA
# Agent — orchestrates job parsing, file-block download, CBOR/base64 decode, signature check and
# self-test, over a consumer-supplied transport.
#
# HAND WRAPPER with an EXPLICIT file list (NOT add_subdirectory, and NOT include(otaFilePaths.cmake)):
# OTA ships no top-level library CMakeLists, and its otaFilePaths.cmake at v3.3.0 include()s
# source/dependency/coreJSON/jsonFilePaths.cmake and defines TINYCBOR_SOURCES pointing at OTA's
# OWN bundled coreJSON/tinycbor submodules — which we deliberately do not init, using the project's
# corejson_config / tinycbor_config instead. So we list OTA's own sources here (they are few and
# stable) and link the project libraries. `ota_config` is the INTERFACE umbrella.
#
# Dependencies (the OTA lib only — the MQTT/HTTP *control & data planes* are injected by the
# consumer through OtaMqttInterface_t / OtaHttpInterface_t, so OTA does NOT link coreMQTT/coreHTTP):
#   corejson_config  -> core_json.h     (ota.c parses the job document)
#   tinycbor_config  -> cbor.h          (ota_cbor.c decodes the MQTT data-plane file blocks)
#   freertos_config  -> FreeRTOS.h / queue.h / timers.h  (the FreeRTOS OS port, ota_os_freertos.c)
# OS port: only the FreeRTOS port (ota_os_freertos.c); the POSIX one (ota_os_posix.c) is left out.
#
# Cross-platform: no chip gate, but the FreeRTOS OS port needs kernel headers — if a platform's
# freertos_config does not re-export queue.h/timers.h, also link freertos-kernel_config there.
set(_ota "${CMAKE_CURRENT_SOURCE_DIR}/ota/source")

add_library(aws_ota STATIC EXCLUDE_FROM_ALL
    "${_ota}/ota.c"
    "${_ota}/ota_interface.c"
    "${_ota}/ota_base64.c"
    "${_ota}/ota_mqtt.c"
    "${_ota}/ota_cbor.c"
    "${_ota}/ota_http.c"
    "${_ota}/portable/os/ota_os_freertos.c"
)
# PUBLIC (SYSTEM): OTA's source/include + source/portable, and this dir for ota_config.h
# (ota.c pulls ota_config_defaults.h -> ota_config.h). PRIVATE: source/ internals (ota_private.h).
target_include_directories(aws_ota SYSTEM PUBLIC
    "${_ota}/include"
    "${_ota}/portable"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_include_directories(aws_ota SYSTEM PRIVATE "${_ota}")
target_link_libraries(aws_ota PUBLIC corejson_config tinycbor_config freertos_config)
target_compile_options(aws_ota PRIVATE -w)

# Umbrella INTERFACE (伞形): consumers link `ota_config` to get the OTA Agent.
add_library(ota_config INTERFACE)
target_link_libraries(ota_config INTERFACE aws_ota)
