# picotls_config — picotls (submodule component/picotls_config/picotls): a TLS 1.3 / QUIC
# crypto+handshake library. Hand wrapper (the upstream CMake is host-oriented: OpenSSL probe,
# brotli/dtrace, test exes). We build only the two pieces the QUIC stack needs, against the
# upstream mbedTLS already on this platform — this is the wolfSSL replacement for ngtcp2's TLS.
#
#   picotls_core      lib/picotls.c + pembase64.c + hpke.c  — the crypto-agnostic TLS 1.3 core.
#   picotls_mbedtls   lib/mbedtls.c + mbedtls_sign.c        — the mbedTLS/PSA crypto backend
#                     (ptls_mbedtls_*: AES-GCM / SHA / X25519 / secp256r1 via PSA, ChaCha20,
#                     ECDH). Compiled with -DPTLS_HAVE_MBEDTLS=1, links `mbedcrypto`.
#
# The PSA backend needs MBEDTLS_PSA_CRYPTO_C (on in the AmebaD config — see
# component/amebad/mbedtls_port). Call psa_crypto_init() once at runtime before use. The minicrypto
# / openssl / fusion backends and the cifra/micro-ecc deps are intentionally not built.
#
# Cross-platform definition (links `mbedcrypto` by name); EXCLUDE_FROM_ALL until a consumer
# (ngtcp2's picotls crypto binding) links it. Wired from component/CMakeLists.txt where a real
# mbedcrypto exists.
set(_p "${CMAKE_CURRENT_SOURCE_DIR}/picotls")

# TLS 1.3 core (backend-agnostic).
add_library(picotls_core STATIC EXCLUDE_FROM_ALL
    "${_p}/lib/picotls.c"
    "${_p}/lib/pembase64.c"
    "${_p}/lib/hpke.c"
)
# PRIVATE compat shims: bare-metal newlib lacks <sys/socket.h>/<netinet/in.h>/<arpa/inet.h>,
# which lib/picotls.c includes for peripheral (non-QUIC) ESNI/log helpers. Compile-only types;
# PRIVATE so they never leak onto a consumer's path (the public picotls.h doesn't need them).
target_include_directories(picotls_core SYSTEM PUBLIC  "${_p}/include")
target_include_directories(picotls_core SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compat")
# Drop the ptls_log framework (defaults ON off-Windows): it pulls a pthread mutex
# (PTHREAD_MUTEX_INITIALIZER, gated out in this newlib) + in6addr_any, and is unused by the
# QUIC handshake path. PUBLIC so the picotls build and consumers agree on the API/ABI.
target_compile_definitions(picotls_core PUBLIC PTLS_HAVE_LOG=0)
target_compile_options(picotls_core PRIVATE -w)

# mbedTLS/PSA backend.
add_library(picotls_mbedtls STATIC EXCLUDE_FROM_ALL
    "${_p}/lib/mbedtls.c"
    "${_p}/lib/mbedtls_sign.c"
)
target_link_libraries(picotls_mbedtls PUBLIC picotls_core mbedcrypto)
target_compile_definitions(picotls_mbedtls PUBLIC PTLS_HAVE_MBEDTLS=1)
target_compile_options(picotls_mbedtls PRIVATE -w)

# Umbrella INTERFACE (伞形): consumers link `picotls_config` to get core + the mbedTLS backend.
add_library(picotls_config INTERFACE)
target_link_libraries(picotls_config INTERFACE picotls_mbedtls picotls_core)
