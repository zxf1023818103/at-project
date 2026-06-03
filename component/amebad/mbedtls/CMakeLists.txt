# amebad mbedtls — build the SHARED upstream Mbed TLS v3.6.4
# (component/thirdparty/mbedtls, the same copy Bouffalo uses) for the AmebaD KM4
# image2, instead of the SDK's in-tree mbedtls-2.4.0/3.x. This is the FreeRTOS
# model extended to mbedtls: one clean upstream lib, per-platform config + port.
#
# Config: reuses Bouffalo's config-tls-generic.h verbatim (it is fully
# Kconfig-gated and platform-clean) so the TLS feature surface matches across
# platforms — only the CONFIG_MBEDTLS_* wall below + the mem header differ. We do
# NOT set the CONFIG_MBEDTLS_*_USE_HW knobs, so crypto is pure software here (no
# Realtek HW-accel yet). Entropy (Realtek TRNG via mbedtls_hardware_poll) and
# net_sockets (over lwip) are NOT wired yet — they are only needed at final-exe
# link time, so the mbedcrypto / mbedtls / mbedx509 .a archives build without
# them. KM4 only (TLS is HP-side).

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

# --- mbedtls config: INTERFACE target carrying the config header path + the
# MBEDTLS_CONFIG_FILE name + the CONFIG_MBEDTLS_* feature wall. Mirrors
# bflb_mbedtls_config (minus the *_USE_HW knobs -> software crypto).
add_library(amebad_mbedtls_config INTERFACE)
target_include_directories(amebad_mbedtls_config INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/config")
target_compile_definitions(amebad_mbedtls_config INTERFACE
    MBEDTLS_CONFIG_FILE="config-tls-generic.h")
target_compile_definitions(amebad_mbedtls_config INTERFACE
    CONFIG_MBEDTLS_TLS_VERSION_1_2
    CONFIG_MBEDTLS_SSL_ALPN
    CONFIG_MBEDTLS_PSK_MAX_LEN=32
    CONFIG_MBEDTLS_PK_WRITE_C
    CONFIG_MBEDTLS_KEY_EXCHANGE_PSK_ENABLED
    CONFIG_MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
    CONFIG_MBEDTLS_KEY_EXCHANGE_DHE_RSA_ENABLED
    CONFIG_MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
    CONFIG_MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
    CONFIG_MBEDTLS_KEY_EXCHANGE_ECDH_ECDSA_ENABLED
    CONFIG_MBEDTLS_KEY_EXCHANGE_ECDH_RSA_ENABLED
    CONFIG_MBEDTLS_PEM_CERTIFICATE_FORMAT
    CONFIG_MBEDTLS_ECDH_C
    CONFIG_MBEDTLS_ECDSA_C
    CONFIG_MBEDTLS_ECP_C
    CONFIG_MBEDTLS_ECP_DP_SECP256R1_ENABLED
    CONFIG_MBEDTLS_ECP_NIST_OPTIM
    CONFIG_MBEDTLS_PKCS5_C
    CONFIG_MBEDTLS_CIPHER_AES_ENABLED
    CONFIG_MBEDTLS_AES_ROM_TABLES
    CONFIG_MBEDTLS_AES_FEWER_TABLES
    CONFIG_MBEDTLS_CIPHER_DES_ENABLED
    CONFIG_MBEDTLS_CIPHER_GCM_ENABLED
    CONFIG_MBEDTLS_CIPHER_MODE_CBC_ENABLED
    CONFIG_MBEDTLS_CIPHER_MODE_CTR_ENABLED
    CONFIG_MBEDTLS_MD5
    CONFIG_MBEDTLS_SHA1
    CONFIG_MBEDTLS_SHA224
    CONFIG_MBEDTLS_SHA256
    CONFIG_MBEDTLS_SHA256_SMALLER
    CONFIG_MBEDTLS_SHA384
    CONFIG_MBEDTLS_SHA512
    CONFIG_MBEDTLS_CTR_DRBG_ENABLED
    CONFIG_MBEDTLS_CIPHER
    CONFIG_MBEDTLS_MD
    CONFIG_MBEDTLS_GENPRIME_ENABLED
    CONFIG_MBEDTLS_HAVE_ASM
    CONFIG_MBEDTLS_ENTROPY_C
    CONFIG_MBEDTLS_ENTROPY_POLL
    CONFIG_MBEDTLS_SERVER_NAME_INDICATION
    CONFIG_MBEDTLS_DHM_C
    CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384)

# --- upstream mbedtls build knobs (set before add_subdirectory). No programs /
# tests (need a hosted env); static libs; warnings not fatal (vendor code).
set(ENABLE_PROGRAMS            OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING            OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS    OFF CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON  CACHE BOOL "" FORCE)

# The config name + dir must be visible to every mbedtls TU.
add_compile_definitions(MBEDTLS_CONFIG_FILE="config-tls-generic.h")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/config")

# EXCLUDE_FROM_ALL: nothing links mbedtls yet (TLS/HTTPS-OTA not wired), so keep
# it out of the default build — a future consumer that links mbedcrypto/mbedtls/
# mbedx509 still pulls it in, or build explicitly: `cmake --build <dir> -t mbedtls`.
add_subdirectory("${CMAKE_SOURCE_DIR}/component/thirdparty/mbedtls"
                 "${CMAKE_BINARY_DIR}/amebad_mbedtls_build"
                 EXCLUDE_FROM_ALL)

# Carry the config (header path + CONFIG_* defs) onto the produced targets so
# their own TUs and downstream consumers compile against the same config.
foreach(_t mbedcrypto mbedtls mbedx509)
    if(TARGET ${_t})
        target_link_libraries(${_t} PUBLIC amebad_mbedtls_config)
    endif()
endforeach()
