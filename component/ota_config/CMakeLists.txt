# ota_config — AWS IoT Over-the-Air Update (submodule component/ota_config/ota @ v3.3.0): the OTA
# Agent — orchestrates job parsing, file-block download, CBOR/base64 decode, signature check and
# self-test, over a consumer-supplied transport.
#
# Source lists come from the upstream otaFilePaths.cmake (so they track the submodule), but the
# JSON/CBOR 3rd-party deps are supplied by the project's shared components, NOT OTA's bundled copies:
#   - otaFilePaths.cmake line 9 hard-include()s ota/source/dependency/coreJSON/jsonFilePaths.cmake,
#     so that nested submodule must be checked out (covered by `git submodule update --init
#     --recursive`). We then list(REMOVE_ITEM) its JSON_SOURCES and the bundled TINYCBOR_SOURCES
#     from OTA_SOURCES, so NONE of OTA's bundled coreJSON/tinycbor is compiled — corejson_config and
#     tinycbor_config provide them (single copy, shared with jobs / coreMQTT / ...).
# OS port: only the FreeRTOS port (OTA_OS_FREERTOS_SOURCES); the POSIX one is left out.
#
# Dependencies (the OTA lib only — the MQTT/HTTP *control & data planes* are injected by the
# consumer through OtaMqttInterface_t / OtaHttpInterface_t, so OTA does NOT link coreMQTT/coreHTTP):
#   corejson_config  -> core_json.h     (ota.c parses the job document)
#   tinycbor_config  -> cbor.h          (ota_cbor.c decodes the MQTT data-plane file blocks)
#   freertos_config  -> FreeRTOS.h / queue.h / timers.h  (the FreeRTOS OS port, ota_os_freertos.c)
#
# Cross-platform: no chip gate, but the FreeRTOS OS port needs kernel headers — if a platform's
# freertos_config does not re-export queue.h/timers.h, also link freertos-kernel_config there.
include("${CMAKE_CURRENT_SOURCE_DIR}/ota/otaFilePaths.cmake")

# Drop OTA's bundled coreJSON + tinycbor from the upstream list; the project links its own.
list(REMOVE_ITEM OTA_SOURCES ${JSON_SOURCES} ${TINYCBOR_SOURCES})

add_library(aws_ota STATIC EXCLUDE_FROM_ALL
    ${OTA_SOURCES}              # ota.c / ota_interface.c / ota_base64.c (+ public headers)
    ${OTA_MQTT_SOURCES}         # ota_mqtt.c / ota_cbor.c
    ${OTA_HTTP_SOURCES}         # ota_http.c
    ${OTA_OS_FREERTOS_SOURCES}  # ota_os_freertos.c
)
# PUBLIC (SYSTEM): OTA's source/include + source/portable, and this dir for ota_config.h
# (ota.c pulls ota_config_defaults.h -> ota_config.h). PRIVATE: source/ internals (ota_private.h).
# The bundled coreJSON/tinycbor include dirs are intentionally NOT added — core_json.h / cbor.h
# come from corejson_config / tinycbor_config.
target_include_directories(aws_ota SYSTEM PUBLIC
    ${OTA_INCLUDE_PUBLIC_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_include_directories(aws_ota SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ota/source")
target_link_libraries(aws_ota PUBLIC corejson_config tinycbor_config freertos_config)
target_compile_options(aws_ota PRIVATE -w)

# Umbrella INTERFACE (伞形): consumers link `ota_config` to get the OTA Agent.
add_library(ota_config INTERFACE)
target_link_libraries(ota_config INTERFACE aws_ota)
