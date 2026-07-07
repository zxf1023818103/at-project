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
    aws_connect.c
)
target_include_directories(aws_provision_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(aws_provision_core PUBLIC FreeRTOS mbedtls_config freertos_cli)
# aws_connect.c needs coreMQTT (MQTTConnectInfo_t / TransportInterface_t) + the TLS transport header.
# The TLS_FreeRTOS_* symbols resolve at the final image link (both AWS images link network_transport).
target_link_libraries(aws_provision_core PRIVATE coremqtt_config)   # core_mqtt.h / core_mqtt_config.h
target_include_directories(aws_provision_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../freertos-cli_config/freertos-cli"
    "${CMAKE_CURRENT_SOURCE_DIR}/../coremqtt_config/coremqtt/source/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../coremqtt_config/coremqtt/source/interface"
    "${CMAKE_CURRENT_SOURCE_DIR}/../network_transport"
)

# --- default key backend: plain LittleFS DER file --------------------------------------------------
add_library(aws_secret_store_file STATIC EXCLUDE_FROM_ALL
    aws_secret_store_file.c
)
target_link_libraries(aws_secret_store_file PUBLIC aws_provision_core)

# --- bl602 AWS client ------------------------------------------------------------------------------
# The TLS backend is switchable via BOUFFALO_TLS_BACKEND (project/bouffalo/Makefile TLS_BACKEND):
# mbedtls (default, IN 6144 -- fits bl602's heap) or picotls (TLS 1.3; larger buffers, may not fit).
# Both export the same TLS_FreeRTOS_* API; transport_tls.h resolves to the linked backend's header.
if(BOUFFALO_TLS_BACKEND STREQUAL "picotls")
    set(_aws_tls network_transport_picotls)
else()
    set(_aws_tls network_transport_mbedtls)
endif()

if(AT_ENABLE_AWS_OTA)
    # AWSOTA=ON: the full coreMQTT-Agent + FreeRTOS OTA v3.3.0 agent (MQTT data plane), sharing AmebaD's
    # orchestration (aws_iot_demo.c) + ota_app/ota_pal + agent support (${AWS_OTA_SOURCES}, from
    # component/aws_ota_config). bl602 knobs via -D: MQTT data plane (OTA_APP_ENABLE_HTTP=0, no 2nd TLS),
    # default TLS backend (no picotls forcing / rx reserve), CA from the provisioned env "ca" file.
    add_library(aws_iot STATIC EXCLUDE_FROM_ALL
        aws_iot_demo.c
        ${AWS_OTA_SOURCES}
    )
    # bl602 heap is tight (~45 KB free after WiFi+DHCP). AWS_STATIC_TASKS puts the agent + OTA task
    # stacks in .bss so a fragmented heap can't fail their contiguous allocation (the observed
    # xTaskCreate failure). AWS_NO_TELEMETRY drops the optional demo publish task (~4 KB). The agent
    # stack is 8 KB (2048 words) - the proven bl602 TLS-handshake peak - vs AmebaD's 20 KB.
    target_compile_definitions(aws_iot PRIVATE
        OTA_APP_ENABLE_HTTP=0
        AWS_TLS_RX_RESERVE=0
        AWS_TLS_BACKEND_MODE=0
        AWS_CA_USE_ENV
        AWS_STATIC_TASKS
        AWS_NO_TELEMETRY
        AWS_AGENT_STACK_WORDS=4096   # 16 KB: the mbedTLS handshake's cert-chain verify + crypto peaks
                                     # ~12 KB; 8 KB overflowed -> HardFault reboot mid-handshake. .bss
                                     # cost is fine now the OTA buffers are dynamic (~45 KB free heap).
        AWS_NET_BUFFER_SZ=2560       # 2 KB OTA blocks (otaconfigLOG2_FILE_BLOCK_SIZE=11 on bl602)
        AWS_KEY_MAX=1536             # RSA-2048 key DER ~1218 B; trims s_key .bss (grows the TLS heap)
    )
    # Same key-buffer trim on the shared provisioning lib (aws_provision.c s_der / aws_load_key).
    target_compile_definitions(aws_provision_core PRIVATE AWS_KEY_MAX=1536)
    target_include_directories(aws_iot PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(aws_iot PUBLIC
        aws_provision_core
        aws_secret_store_file
        coremqtt_config
        coremqtt-agent_config
        ota_config
        aws_ota_config          # ota_flash.h (via ota_flash_config) + subscription_manager/agent_message/command_pool headers
        backoffalgorithm_config
        rxi_log_config          # at_log.h (the project logger; aws_iot_demo.c + ota_app.c log through it)
        ${_aws_tls}
        mbedtls_config
        freertos_cli
    )
    target_include_directories(aws_iot PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../coremqtt_config/coremqtt/source/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../coremqtt_config/coremqtt/source/interface"
        "${CMAKE_CURRENT_SOURCE_DIR}/../coremqtt-agent_config"
        "${CMAKE_CURRENT_SOURCE_DIR}/../coremqtt-agent_config/coremqtt-agent/source/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../ota_config"
        "${CMAKE_CURRENT_SOURCE_DIR}/../ota_config/ota/source/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../ota_config/ota/source/portable/os"
        "${CMAKE_CURRENT_SOURCE_DIR}/../network_transport"
        "${CMAKE_CURRENT_SOURCE_DIR}/../freertos-cli_config/freertos-cli"
    )
else()
    # Default: the lean telemetry-only client (direct coreMQTT over the TLS transport; no agent/OTA).
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
endif()
