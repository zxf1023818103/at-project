# component/aws_ota_config - the shared AWS IoT OTA Agent wiring: the FreeRTOS OTA agent glue
# (ota_app.c: OtaInterfaces_t + the coreMQTT-Agent subscribe/publish wrappers + OTA task), the OTA PAL
# (ota_pal.c: streams blocks into the OTA slot writer ota_flash.h + mbedTLS ECDSA code-sign verify),
# and the coreMQTT-Agent support TUs (message queue + command pool + subscription dispatch). Like
# ota_http, the SOURCES compile into each platform's app (per-image config). The app links the heavy
# deps it already pulls for AWS: coremqtt-agent_config, ota_config, corejson_config, tinycbor_config,
# mbedtls_config, network_transport. Slot writer = the platform ota_flash_config impl (AmebaD
# ota_flash.c / Bouffalo ota_flash_port).
add_library(aws_ota_config INTERFACE)
target_include_directories(aws_ota_config INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(aws_ota_config INTERFACE ota_flash_config)

set(AWS_OTA_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/ota_app.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ota_pal.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/freertos_agent_message.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/freertos_command_pool.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/subscription_manager.c"
    CACHE INTERNAL "shared AWS OTA agent sources (compiled per-app)")
