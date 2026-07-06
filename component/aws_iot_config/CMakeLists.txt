# component/aws_iot_config - shared AWS IoT code (AmebaD + Bouffalo).
#
#   aws_provision_core   : shared provisioning (base64->DER, cert/CA files, key via secret-store) +
#                          the `aws` console command. Both platforms link this.
#   aws_secret_store_file: default key backend = plain LittleFS DER file (bl602; AmebaD non-TF-M).
#                          AmebaD TF-M compiles aws_secret_store_its.c instead (from the amebad build,
#                          which supplies the PSA/ITS headers) - it overrides the file backend at link.
#   aws_iot              : bl602 telemetry client = single-task coreMQTT over the mbedTLS TLS transport
#                          (no coreMQTT-Agent / OTA - those don't fit bl602's heap). AmebaD's connect/
#                          publish (coreMQTT-Agent + OTA) stays in project/amebad/app_km4.
add_compile_options("-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=")

# --- shared provisioning + CLI (both platforms) --------------------------------------------------
add_library(aws_provision_core STATIC EXCLUDE_FROM_ALL
    aws_provision.c
    aws_cli.c
)
target_include_directories(aws_provision_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(aws_provision_core PUBLIC FreeRTOS mbedtls_config freertos_cli)
target_include_directories(aws_provision_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../freertos-cli_config/freertos-cli"
)

# --- default key backend: plain LittleFS DER file --------------------------------------------------
add_library(aws_secret_store_file STATIC EXCLUDE_FROM_ALL
    aws_secret_store_file.c
)
target_link_libraries(aws_secret_store_file PUBLIC aws_provision_core)

# --- bl602 telemetry client ------------------------------------------------------------------------
# The TLS backend is switchable via BOUFFALO_TLS_BACKEND (project/bouffalo/Makefile TLS_BACKEND):
# mbedtls (default, IN 6144 -- fits bl602's heap) or picotls (TLS 1.3; larger buffers, may not fit).
# Both export the same TLS_FreeRTOS_* API; transport_tls.h resolves to the linked backend's header.
if(BOUFFALO_TLS_BACKEND STREQUAL "picotls")
    set(_aws_tls network_transport_picotls)
else()
    set(_aws_tls network_transport_mbedtls)
endif()
add_library(aws_iot STATIC EXCLUDE_FROM_ALL
    aws_telemetry.c
)
target_include_directories(aws_iot PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(aws_iot PUBLIC
    aws_provision_core
    aws_secret_store_file
    coremqtt_config
    ${_aws_tls}
    mbedtls_config
    freertos_cli
)
target_include_directories(aws_iot PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../coremqtt_config/coremqtt/source/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../coremqtt_config/coremqtt/source/interface"
    "${CMAKE_CURRENT_SOURCE_DIR}/../network_transport"
    "${CMAKE_CURRENT_SOURCE_DIR}/../freertos-cli_config/freertos-cli"
)
