# bflb_mbedtls_config — INTERFACE target that exports the Bouffalo
# mbedtls config header search path. Upstream Mbed TLS compiles every
# TU with -DMBEDTLS_CONFIG_FILE=<some name>; we set that macro in
# the mbedtls block of component/thridparty/CMakeLists.txt to
# "config-tls-generic.h" — and this directory is what makes that name resolve.
#
# Why upstream is left clean: the Bouffalo config (config-tls-generic.h)
# is functionally equivalent to a fork of upstream's mbedtls_config.h
# with feature toggles wired to the CONFIG_MBEDTLS_* macros that the
# project sets at build time. Keeping it in its own component dir means
# the mbedtls submodule pin stays vanilla upstream.
#
# Files preserved verbatim from bouffalo_sdk/components/crypto/mbedtls/:
#   config-tls-generic.h          the active config consumed by upstream
#   config-psa.h                  PSA crypto config (included by above)
#   Kconfig.tls-generic           the Kconfig schema the macros come from
#   kvs_webrtc_mbedtls_config.h   alternate KVS-WebRTC profile, unused by
#                                 default but kept for completeness.
#   mbedtls_port_bouffalo_sdk.h   tiny header pointed at by
#                                 MBEDTLS_PLATFORM_STD_MEM_HDR — wires
#                                 calloc/free/printf/snprintf macros so
#                                 mbedtls calls the C standard library.
#                                 Logically part of the config surface
#                                 (moved out of port/ to keep the dep
#                                 graph tidy — port doesn't need to be
#                                 on every mbedtls consumer's include line).

add_library(bflb_mbedtls_config INTERFACE)

target_include_directories(bflb_mbedtls_config INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Propagate MBEDTLS_CONFIG_FILE so every consumer (port, hw_acc, upstream
# mbedcrypto/mbedtls/mbedx509, the supplicant TUs that #include
# <mbedtls/aes.h>, …) sees the same config. The mbedtls block in
# component/thridparty/CMakeLists.txt also sets this via
# add_compile_definitions, but only the post-include targets get covered
# by that — INTERFACE propagation through this
# link catches the targets added earlier (hw_acc, port, the wifi4
# supplicant, etc.).
target_compile_definitions(bflb_mbedtls_config INTERFACE
    MBEDTLS_CONFIG_FILE="config-tls-generic.h"
)

# config-tls-generic.h gates the MBEDTLS_*_C primitives on a wall of
# CONFIG_MBEDTLS_* macros that the SDK's autoconf used to set. Mirror
# the set the SDK CMakeLists turned on by default; consumers can flip
# specific ones off by overriding before linking. This matches what
# Kconfig produced for the wifi4 + TLS use case the project needs.
target_compile_definitions(bflb_mbedtls_config INTERFACE
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
    CONFIG_MBEDTLS_SOME_AEAD_CIPHER_ENABLED
    CONFIG_MBEDTLS_SOME_CIPHER_ENABLED
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
    CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384
)
