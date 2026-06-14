# component/amebad/mbedtls_port — AmebaD KM4 port of the upstream Mbed TLS v3.6.4 build.
#
# The upstream mbedcrypto/mbedx509/mbedtls targets themselves are built by
# component/mbedtls_config (add_subdirectory'd from component/CMakeLists.txt's AmebaD branch,
# BEFORE the amebad subtree). This dir supplies the AmebaD-specific pieces:
#   - `amebad_mbedtls_cfg` INTERFACE: the MBEDTLS_USER_CONFIG_FILE + MBEDTLS_*_ALT selections +
#     the include dirs, attached PUBLIC onto the upstream targets so their sources compile with
#     our deltas. The shared config (user config, threading_alt.h, the FreeRTOS threading-alt
#     source) now lives in component/mbedtls_config; only the AmebaD-specific entropy
#     (entropy_hardware_alt.c) and the HW *_ALT impls (gcm_alt/chachapoly_alt) stay here.
#   - the runtime glue lib `amebad_mbedtls_port` (entropy + shared threading-alt + HW ALTs).
#
# Consumers link the `amebad_mbedtls` umbrella (crypto + x509 + glue). This is the modern
# software crypto backend for corePKCS11 today and the planned picotls/ngtcp2 QUIC stack
# (replacing wolfSSL). Coexists with the ROM Mbed TLS 2.4.0 path (mbedtls_rom) — separate
# targets, never linked into the same image.
if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

# Config surface attached PUBLIC onto the upstream mbedcrypto/mbedx509 targets: the
# MBEDTLS_USER_CONFIG_FILE + the MBEDTLS_*_ALT selections (as -D, so the HW-crypto ones can be
# gated on a build option) + the include dirs for the headers their sources pull by bare name:
#   - mbedtls_amebad_user_config.h + threading_alt.h  -> component/mbedtls_config (MBEDTLS_CONFIG_DIR)
#   - gcm_alt.h / chachapoly_alt.h (the HW *_ALT contexts) -> this dir
add_library(amebad_mbedtls_cfg INTERFACE)
target_compile_definitions(amebad_mbedtls_cfg INTERFACE
    MBEDTLS_USER_CONFIG_FILE="mbedtls_amebad_user_config.h"
    MBEDTLS_THREADING_ALT            # FreeRTOS mutex: threading_alt.h + mbedtls_threading_alt.c
    MBEDTLS_ENTROPY_HARDWARE_ALT)    # mbedtls_hardware_poll(): entropy_hardware_alt.c
target_include_directories(amebad_mbedtls_cfg SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"    # gcm_alt.h / chachapoly_alt.h
    "${MBEDTLS_CONFIG_DIR}")         # mbedtls_amebad_user_config.h / threading_alt.h

# Opt-in: route the AEAD ciphers to the RTL8721D IPSEC HW engine — MBEDTLS_GCM_ALT (gcm_alt.{h,c})
# + MBEDTLS_CHACHAPOLY_ALT (chachapoly_alt.{h,c}); both NS ROM, NSPE-direct. AES-GCM ~115x the
# software GCM (the QUIC record-cipher bottleneck). Needs TFM=ON (NS-direct reaches the
# secure-only crypto peripheral only with TF-M's SAU).
# The OTHER *_ALT modules are left software on purpose (the engine is ONE shared single-context
# DMA block): SHA256/512_ALT - mbedTLS clones/keeps concurrent SHA contexts (TLS transcript) that
# one HW context can't hold (and SW SHA-256 is already ~50 cyc/B); AES_ALT - per-16B-block DMA is
# an anti-pattern (the win is at the AEAD level); CCM/DES/MD5 - not in ROM / not used by TLS1.3.
option(AMEBAD_MBEDTLS_HW_GCM "Route mbedTLS AEAD (GCM + ChaCha20-Poly1305) to the RTL8721D IPSEC HW engine" OFF)
if(AMEBAD_MBEDTLS_HW_GCM)
    target_compile_definitions(amebad_mbedtls_cfg INTERFACE MBEDTLS_GCM_ALT MBEDTLS_CHACHAPOLY_ALT)
endif()

# Attach the config onto the upstream targets (defined earlier by component/mbedtls_config).
# PUBLIC so it reaches both the targets' own sources AND every consumer (corePKCS11, picotls).
foreach(_t mbedcrypto mbedx509 mbedtls p256m everest)
    if(TARGET ${_t})
        target_link_libraries(${_t} PUBLIC amebad_mbedtls_cfg)
        target_compile_options(${_t} PRIVATE -w)   # silence vendor warnings under -Werror
    endif()
endforeach()

# Runtime glue lib. entropy_hardware_alt.c (AmebaD-specific entropy) is local; the generic
# FreeRTOS threading-alt (mbedtls_threading_alt.c) is the SHARED source in component/mbedtls_config,
# compiled here so it picks up amebad_mbedtls_cfg's THREADING_ALT + threading_alt.h. gcm_alt.c /
# chachapoly_alt.c supply the mbedtls_gcm_*/chachapoly_* symbols that gcm.c/chachapoly.c omit
# under the ALTs (must be linked), so add them only when the ALTs are on.
set(_amebad_mbedtls_port_srcs
    entropy_hardware_alt.c
    "${MBEDTLS_CONFIG_DIR}/mbedtls_threading_alt.c")
if(AMEBAD_MBEDTLS_HW_GCM)
    list(APPEND _amebad_mbedtls_port_srcs gcm_alt.c chachapoly_alt.c)
endif()
add_library(amebad_mbedtls_port STATIC EXCLUDE_FROM_ALL ${_amebad_mbedtls_port_srcs})
target_link_libraries(amebad_mbedtls_port PUBLIC amebad_mbedtls_cfg mbedcrypto FreeRTOS freertos_config)

# Umbrella (伞形): consumers link `amebad_mbedtls` for crypto + x509 + the glue.
add_library(amebad_mbedtls INTERFACE)
target_link_libraries(amebad_mbedtls INTERFACE mbedcrypto mbedx509 amebad_mbedtls_port amebad_mbedtls_cfg)
