# wolfssl_config — wolfCrypt (from component/thirdparty/wolfssl, v5.9.1-stable)
# built for KM4 (Cortex-M33) as a bare-metal static lib for on-target crypto
# benchmarking. Config lives in user_settings.h (this dir); selected via
# -DWOLFSSL_USER_SETTINGS. See doc/amebad_wolfssl_hwaccel_eval.md.
#
# Two asm tiers, switched by the AMEBAD_WOLFSSL_ARMASM cache option:
#   OFF (Build A): SP Cortex-M asm (PK/handshake) + portable-C symmetric/hash.
#   ON  (Build B): + Thumb2 hand-asm for AES/SHA/ChaCha/Poly1305/Curve25519.
# SP asm (sp_int.c + sp_cortexm.c) is ALWAYS on — it's the handshake headline.
#
# Cross-platform: no chip gate — any platform that links wolfssl_config builds it
# (EXCLUDE_FROM_ALL until then). The SP Cortex-M asm + Thumb2 options below are
# guarded by their own cache vars, so a non-Cortex-M consumer can turn them off.

option(AMEBAD_WOLFSSL_ARMASM "wolfSSL: use Thumb2 asm for symmetric/hash" OFF)
option(AMEBAD_WOLFSSL_SP_ASM "wolfSSL: SP Cortex-M asm for public-key (PK)" ON)
option(AMEBAD_WOLFSSL_TLS    "wolfSSL: build full TLS 1.3 + QUIC layer (for ngtcp2)" OFF)

set(_wolf "${CMAKE_SOURCE_DIR}/component/thirdparty/wolfssl")
set(_wc   "${_wolf}/wolfcrypt/src")

# Curated wolfCrypt source set. PK (sp_cortexm.c = dedicated Cortex-M asm for
# P-256 / RSA-2048-3072; sp_int.c = SP_MATH_ALL generic). Everything else gated
# to ~empty by user_settings.h when its feature is off.
set(_wolf_src
    "${_wc}/aes.c"
    "${_wc}/asn.c"
    "${_wc}/chacha.c"
    "${_wc}/chacha20_poly1305.c"
    "${_wc}/coding.c"
    "${_wc}/curve25519.c"
    "${_wc}/ecc.c"
    "${_wc}/error.c"
    "${_wc}/fe_operations.c"
    "${_wc}/hash.c"
    "${_wc}/hmac.c"
    "${_wc}/logging.c"
    "${_wc}/memory.c"
    "${_wc}/poly1305.c"
    "${_wc}/random.c"
    "${_wc}/rsa.c"
    "${_wc}/sha.c"
    "${_wc}/sha256.c"
    "${_wc}/sha512.c"
    "${_wc}/sp_int.c"
    "${_wc}/wc_encrypt.c"
    "${_wc}/wc_port.c"
    "${_wc}/wolfmath.c"
)

# Build B: Thumb2 inline-asm (_c.c) for the record layer. Compiled as C (no
# separate assembler config) because WOLFSSL_ARMASM_INLINE is set.
if(AMEBAD_WOLFSSL_ARMASM)
    list(APPEND _wolf_src
        "${_wc}/port/arm/thumb2-aes-asm_c.c"
        "${_wc}/port/arm/thumb2-sha256-asm_c.c"
        "${_wc}/port/arm/thumb2-sha512-asm_c.c"
        "${_wc}/port/arm/thumb2-chacha-asm_c.c"
        "${_wc}/port/arm/thumb2-poly1305-asm_c.c"
        "${_wc}/port/arm/thumb2-curve25519_c.c"
    )
endif()

# SP specialized routines for P-256 / RSA-2048/3072 (sp_ecc_*_256, sp_Rsa*):
#   sp_cortexm.c = dedicated Cortex-M assembly (default)
#   sp_c32.c     = portable C (all-portable-C baseline build)
if(AMEBAD_WOLFSSL_SP_ASM)
    list(APPEND _wolf_src "${_wc}/sp_cortexm.c")
else()
    list(APPEND _wolf_src "${_wc}/sp_c32.c")
endif()

# TLS 1.3 + QUIC layer (ngtcp2 backend). ssl.c is the umbrella TU: it #includes
# evp.c, misc.c and the modular ssl_*.c / pk.c / x509.c units, so they are NOT
# listed separately. kdf.c supplies the TLS 1.3 HKDF key schedule.
if(AMEBAD_WOLFSSL_TLS)
    list(APPEND _wolf_src
        "${_wolf}/src/ssl.c"
        "${_wolf}/src/internal.c"
        "${_wolf}/src/keys.c"
        "${_wolf}/src/tls.c"
        "${_wolf}/src/tls13.c"
        "${_wolf}/src/quic.c"
        "${_wolf}/src/wolfio.c"   # SslBioSend/SslBioReceive (BIO I/O refs from internal.c); socket paths gated off by WOLFSSL_NO_SOCK
        "${_wc}/kdf.c"
    )
endif()

add_library(wolfssl_config STATIC EXCLUDE_FROM_ALL ${_wolf_src})

if(AMEBAD_WOLFSSL_TLS)
    target_compile_definitions(wolfssl_config PUBLIC AMEBAD_WOLFSSL_TLS)
endif()

if(NOT AMEBAD_WOLFSSL_SP_ASM)
    target_compile_definitions(wolfssl_config PUBLIC WOLFSSL_AMEBAD_NO_SP_ASM)
endif()

target_include_directories(wolfssl_config SYSTEM PUBLIC
    "${_wolf}"                       # -> <wolfssl/wolfcrypt/...>, <wolfssl/certs_test.h>
    "${CMAKE_CURRENT_SOURCE_DIR}"    # -> "user_settings.h"
)

target_compile_definitions(wolfssl_config PUBLIC WOLFSSL_USER_SETTINGS)
if(AMEBAD_WOLFSSL_ARMASM)
    target_compile_definitions(wolfssl_config PUBLIC WOLFSSL_AMEBAD_ARMASM)
endif()

# FreeRTOS for FreeRTOS.h + pvPortMalloc/vPortFree + mutex (FREERTOS in
# user_settings.h). Vendor lib: silence its warnings (we don't own them).
target_link_libraries(wolfssl_config PUBLIC FreeRTOS)
target_compile_options(wolfssl_config PRIVATE -w)
