# component/quic_selftest_config - the ngtcp2 + picotls/mbedTLS TLS 1.3 QUIC client self-test
# (quic_test.c), shared cross-platform (moved out of project/amebad/app_km4). Linked into an image
# only when QUIC=ON. The server address/port, the task stack (QUIC_TASK_STACK_WORDS), and a heap-tight
# QUIC_SMOKE mode (tiny handshake+download for bl602) are all -D-overridable per chip.
# Randomness is entirely PSA (ptls_mbedtls_random_bytes) -> each platform's mbedtls_port entropy
# (AmebaD placeholder / Bouffalo HW TRNG), so this TU is platform-agnostic.
add_compile_options("-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=")

add_library(quic_selftest STATIC EXCLUDE_FROM_ALL
    quic_test.c
)
target_include_directories(quic_selftest PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# Link the REAL ngtcp2/picotls archives (NOT the ngtcp2_config / picotls_config INTERFACE umbrellas):
# a consumer's flat --start-group lists these as members, and routing through an INTERFACE that itself
# lists group members trips a CMake group<->umbrella cycle (see component/ngtcp2_config notes). These
# PUBLIC links also give quic_test.c its ngtcp2 / picotls / mbedTLS (psa) / FreeRTOS+TCP headers.
target_link_libraries(quic_selftest PUBLIC
    FreeRTOS
    freertos-plus-tcp_config
    ngtcp2_crypto_picotls
    ngtcp2_static
    picotls_core
    picotls_mbedtls
    mbedtls_config
)
# at_log.h (log_info): rxi_log_config is added after this component, so borrow its headers via genex;
# the log_* symbols resolve at the final image link.
target_include_directories(quic_selftest SYSTEM PRIVATE
    $<TARGET_PROPERTY:rxi_log_config,INTERFACE_INCLUDE_DIRECTORIES>
)
target_compile_options(quic_selftest PRIVATE -w)   # QUIC/ngtcp2 glue: silence vendor-ish warnings
