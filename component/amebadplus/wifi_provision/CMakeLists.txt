add_library(amebadplus_wifi_provision STATIC wifi_provision.c)
set(wpa ${AMEBADPLUS_SDK}/component/wifi/wpa_supplicant)
target_include_directories(amebadplus_wifi_provision PRIVATE
    ${wpa}/wpa_supplicant ${wpa}/src/crypto ${wpa}/src/wps)
target_link_libraries(amebadplus_wifi_provision PRIVATE amebadplus_sdk FreeRTOS
    wifi_service freertos_cli mbedtls_config)
if(AT_ENABLE_WPS)
    target_compile_definitions(amebadplus_wifi_provision PRIVATE AT_ENABLE_WPS)
endif()
if(AT_ENABLE_EAP)
    set(wpa ${AMEBADPLUS_SDK}/component/wifi/wpa_supplicant)
    # Adapt the vendor source in the build tree; keep the SDK submodule clean.
    file(READ ${wpa}/src/crypto/tls_polarssl.c tls)
    string(REPLACE "<mbedtls/config.h>" "<mbedtls/build_info.h>" tls "${tls}")
    string(REPLACE "if ((ret = mbedtls_ssl_setup(tls_context->ssl, tls_context->conf)) != 0)"
        "extern const char *dplus_eap_server_name;\n\tif ((ret = mbedtls_ssl_set_hostname(tls_context->ssl, dplus_eap_server_name)) != 0 || (ret = mbedtls_ssl_setup(tls_context->ssl, tls_context->conf)) != 0)"
        tls "${tls}")
    file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/tls_polarssl.c CONTENT "${tls}" @ONLY)
    file(READ ${wpa}/wpa_supplicant/wifi_eap_config.c eap)
    string(REPLACE "#include \"eap_protocol_handler.h\""
        "#include \"eap_protocol_handler.h\"\n#undef ENABLE_EAP_SSL_VERIFY_SERVER\n#define ENABLE_EAP_SSL_VERIFY_SERVER 1"
        eap "${eap}")
    file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/wifi_eap_config.c CONTENT "${eap}" @ONLY)
    target_sources(amebadplus_wifi_provision PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/tls_polarssl.c
        ${CMAKE_CURRENT_BINARY_DIR}/wifi_eap_config.c eap_net.c)
    target_include_directories(amebadplus_wifi_provision PRIVATE
        ${AT_ROOT}/component/mbedtls_config/mbedtls/library)
    target_compile_definitions(amebadplus_wifi_provision PRIVATE AT_ENABLE_EAP MBEDTLS_ALLOW_PRIVATE_ACCESS)
    target_compile_options(amebadplus_wifi_provision PRIVATE -w)
endif()
