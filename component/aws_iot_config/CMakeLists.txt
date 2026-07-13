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
    aws_mac.c        # shared aws_get_mac (Thing name = BT MAC via at_netif_bt_mac)
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
# The TLS backend is switchable via TLS_BACKEND (project/bouffalo/Makefile TLS_BACKEND):
# picotls (TLS 1.3, the AWSOTA default -- connects to AWS IoT), mbedtls (TLS 1.2/1.3 -- state=12 on AWS),
# or fallback (network_transport_fallback: co-links both, picotls-first). All export the same
# TLS_FreeRTOS_* API; transport_tls.h resolves to the linked backend's header (fallback -> both backends'
# symbols renamed, see component/network_transport/CMakeLists.txt).
if(TLS_BACKEND STREQUAL "picotls")
    set(_aws_tls network_transport_picotls)
elseif(TLS_BACKEND STREQUAL "fallback")
    set(_aws_tls network_transport_fallback)
else()
    set(_aws_tls network_transport_mbedtls)
endif()

# Firmware version string for the telemetry `fw` field + the OTA version print (bouffalo Makefile
# FW_VERSION -> -DAWS_FW_VERSION; amebad stamps the same AT_FW_VERSION via its own set_property).
# Unset -> the TU default ("dev", aws_telemetry_common.h).
if(NOT DEFINED AWS_FW_VERSION OR AWS_FW_VERSION STREQUAL "")
    set(_aws_fw_def "")
else()
    set(_aws_fw_def "AT_FW_VERSION=\"${AWS_FW_VERSION}\"")
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
    # OTA firmware version-gate build number (ota_app.c appFirmwareVersion .build), from the bl602
    # Makefile APP_BUILD flag; an OTA target must exceed the running build. Falls back to the
    # ota_app.c default (2) when not passed.
    if(NOT DEFINED AWS_APP_BUILD OR AWS_APP_BUILD STREQUAL "")
        set(AWS_APP_BUILD 2)
    endif()
    # bl602 heap is tight (~45 KB free after WiFi+DHCP). AWS_STATIC_TASKS puts the agent + OTA task
    # stacks in .bss so a fragmented heap can't fail their contiguous allocation (the observed
    # xTaskCreate failure). The telemetry publish task is always included — uniform across
    # platforms (no AWS_NO_TELEMETRY carve-outs anymore).
    target_compile_definitions(aws_iot PRIVATE
        APP_VERSION_BUILD=${AWS_APP_BUILD}   # OTA version gate (bl602 Makefile APP_BUILD; > running build)
        AWS_TLS_RX_RESERVE=0
        AWS_TLS_BACKEND_MODE=0
        AWS_CA_USE_ENV
        AWS_STATIC_TASKS
        AWS_AGENT_STACK_WORDS=3584   # 14 KB: the cert-chain verify + software RSA-2048 modexp peaks
                                     # ~12 KB (8 KB overflowed -> HardFault). Trimmed from 16 KB to give
                                     # 2 KB of that .bss back to the heap for the modexp (which needs a
                                     # comfortable margin or it corrupts near-OOM). configCHECK=2 catches
                                     # an overflow cleanly if 14 KB proves too tight.
        AWS_NET_BUFFER_SZ=1536       # holds one incoming 1 KB OTA-block CBOR PUBLISH + MQTT overhead
                                     # (bl602 uses 1 KB blocks, otaconfigLOG2_FILE_BLOCK_SIZE=10)
        AWS_NET_BUFFER_DYNAMIC       # malloc the 2.5 KB MQTT buffer AFTER the TLS handshake (not .bss)
                                     # so the heap-tight server-cert RSA-2048 verify (software modexp,
                                     # since the BL602 HW PKA mis-computes a 2048-bit modulus) has room.
        AWS_KEY_MAX=1536            # client-key DER buffer (aws_iot_demo.c s_key): 1536 holds an EC P-256 key
                                     # (~138 B) OR an RSA-2048 key (~1192 B), so bl602 supports both. The +1 KB
                                     # .bss vs the EC-only 512 is affordable on a clean ~49 KB handshake heap
                                     # (the earlier build-3 OOM at 32 KB was a degraded post-OTA/churn state).
    )
    # AWS_CERT_MAX sizes the base64-decode scratch (s_der in aws_provision.c), reused for cert/CA/key.
    # 1536 holds the largest credential DER: an RSA-2048 key ~1192 B (> RSA cert ~861 B / CA ~837 B / EC
    # key ~138 B). Needed to base64-decode an RSA client key during `aws key`; kept equal to AWS_KEY_MAX.
    target_compile_definitions(aws_provision_core PRIVATE AWS_KEY_MAX=1536 AWS_CERT_MAX=1536)
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
        corehttp_config         # HTTP data plane (presigned-S3 download) — uniform on every platform
        ota_http_config         # urlparse.h + ota_http_interface.h for ota_app.c's S3 fetcher
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

if(NOT _aws_fw_def STREQUAL "")
    target_compile_definitions(aws_iot PRIVATE "${_aws_fw_def}")   # both variants (telemetry `fw` field)
endif()
