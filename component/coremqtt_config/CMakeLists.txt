# coremqtt_config — FreeRTOS coreMQTT (component/coremqtt_config/coremqtt, v5.0.2) built as
# a cross-platform static lib, with the project's core_mqtt_config.h (this dir) on the
# include path. Mirrors the wolfssl_config / ngtcp2_config arrangement: the third-party
# source stays untouched under thirdparty/, the build glue + config live here.
#
# Cross-platform: no chip gate. coreMQTT is transport-agnostic — the consumer supplies
# the TransportInterface_t (send/recv fn ptrs) and the MQTTGetCurrentTimeFunc_t — so the
# library itself has no platform dependency. EXCLUDE_FROM_ALL until a consumer links it.
set(_mqtt "${CMAKE_SOURCE_DIR}/component/coremqtt_config/coremqtt/source")

add_library(coremqtt_config STATIC EXCLUDE_FROM_ALL
    "${_mqtt}/core_mqtt.c"
    "${_mqtt}/core_mqtt_state.c"
    "${_mqtt}/core_mqtt_serializer.c"
    "${_mqtt}/core_mqtt_prop_serializer.c"
    "${_mqtt}/core_mqtt_prop_deserializer.c"
    "${_mqtt}/core_mqtt_serializer_private.c"
)

# PUBLIC: core_mqtt.h pulls core_mqtt_config_defaults.h -> core_mqtt_config.h, so every
# consumer of <core_mqtt.h> needs this dir on its path too (same as wolfssl_config's
# user_settings.h). SYSTEM to keep upstream's headers out of -Werror.
target_include_directories(coremqtt_config SYSTEM PUBLIC
    "${_mqtt}/include"               # -> <core_mqtt.h>, <core_mqtt_serializer.h>, *_defaults.h
    "${_mqtt}/interface"             # -> <transport_interface.h>
    "${CMAKE_CURRENT_SOURCE_DIR}"    # -> "core_mqtt_config.h"
)

# Vendor source: silence its warnings under the SDK's strict -Wall/-Wextra/-Werror
# stack — same treatment as zlib / cjson / littlefs / wolfssl_config.
target_compile_options(coremqtt_config PRIVATE -w)
