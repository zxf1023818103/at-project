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
#   - the runtime glue lib `mbedtls_port` (entropy + HW ALTs). The shared FreeRTOS threading-alt
#     is compiled into the `mbedtls_config` lib over in component/mbedtls_config.
#
# Consumers link the `mbedtls_config` umbrella (crypto + x509 + tls + this glue). This is the modern
# software crypto backend for corePKCS11 today and the planned picotls/ngtcp2 QUIC stack
# (replacing wolfSSL). Coexists with the ROM Mbed TLS 2.4.0 path (mbedtls_rom) — separate
# targets, never linked into the same image.
if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

# Config surface attached PUBLIC onto the upstream mbedcrypto/mbedx509 targets: the MBEDTLS_*_ALT
# selections (as -D, so the HW-crypto ones can be gated on a build option) + the include dirs for
# the headers their sources pull by bare name. (MBEDTLS_USER_CONFIG_FILE itself is selected in
# component/mbedtls_config, next to the mbedtls_amebad_user_config.h it names.)
#   - threading_alt.h                                  -> component/mbedtls_config (MBEDTLS_CONFIG_DIR)
#   - gcm_alt.h / chachapoly_alt.h (the HW *_ALT contexts) -> this dir
add_library(amebad_mbedtls_cfg INTERFACE)
target_compile_definitions(amebad_mbedtls_cfg INTERFACE
    MBEDTLS_THREADING_ALT            # FreeRTOS mutex: threading_alt.h + mbedtls_threading_alt.c
    MBEDTLS_ENTROPY_HARDWARE_ALT)    # mbedtls_hardware_poll(): entropy_hardware_alt.c
target_include_directories(amebad_mbedtls_cfg SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"    # gcm_alt.h / chachapoly_alt.h
    "${MBEDTLS_CONFIG_DIR}")         # threading_alt.h

# Opt-in: route the AEAD ciphers to the RTL8721D IPSEC HW engine — MBEDTLS_GCM_ALT (gcm_alt.{h,c})
# + MBEDTLS_CHACHAPOLY_ALT (chachapoly_alt.{h,c}); both NS ROM, NSPE-direct. AES-GCM ~115x the
# software GCM (the QUIC record-cipher bottleneck). Needs TFM=ON (NS-direct reaches the
# secure-only crypto peripheral only with TF-M's SAU).
# The OTHER *_ALT modules are left software on purpose (the engine is ONE shared single-context
# DMA block): SHA256/512_ALT - mbedTLS clones/keeps concurrent SHA contexts (TLS transcript) that
# one HW context can't hold (and SW SHA-256 is already ~50 cyc/B); AES_ALT - per-16B-block DMA is
# an anti-pattern (the win is at the AEAD level); CCM/DES/MD5 - not in ROM / not used by TLS1.3.
# Default ON: HW AEAD is the preferred path. The Makefile drives the real value off HWGCM (which
# itself defaults to TFM, since NS-direct HW crypto needs TF-M's SAU), so a non-TFM image still
# gets it OFF and never touches the secure-only engine; this default just makes ON the baseline.
option(AMEBAD_MBEDTLS_HW_GCM "Route mbedTLS AEAD (GCM + ChaCha20-Poly1305) to the RTL8721D IPSEC HW engine" ON)
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

# Runtime glue lib (`mbedtls_port`). entropy_hardware_alt.c (AmebaD-specific entropy) is always
# in. gcm_alt.c / chachapoly_alt.c supply the mbedtls_gcm_*/chachapoly_* symbols that
# gcm.c/chachapoly.c omit under MBEDTLS_GCM_ALT / MBEDTLS_CHACHAPOLY_ALT; both are #if-guarded
# internally (compile to empty TUs when the ALTs are off), so they're listed unconditionally —
# the sources go straight into add_library, no list variable (see feedback_cmake_explicit_simple).
# The generic FreeRTOS threading-alt (mbedtls_threading_alt.c) is compiled by the `mbedtls_config`
# lib over in component/mbedtls_config, not here.
add_library(mbedtls_port STATIC EXCLUDE_FROM_ALL
    entropy_hardware_alt.c
    gcm_alt.c
    chachapoly_alt.c)
target_link_libraries(mbedtls_port PUBLIC amebad_mbedtls_cfg mbedcrypto FreeRTOS freertos_config)

# Attach the AmebaD HW-ALT/entropy glue onto the shared `mbedtls_config` umbrella (defined earlier
# in component/mbedtls_config). PUBLIC so a consumer linking just `mbedtls_config` also pulls the
# port's gcm_alt/chachapoly_alt/entropy symbols. mbedtls_config is the single umbrella now (it
# already re-exports mbedcrypto/mbedx509/mbedtls); no separate amebad_mbedtls aggregate.
target_link_libraries(mbedtls_config mbedtls_port)   # plain sig (matches mbedtls_config/CMakeLists.txt)
