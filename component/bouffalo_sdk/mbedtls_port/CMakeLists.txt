# component/bouffalo_sdk/mbedtls_port — Bouffalo BL602/BL616/BL616CL port of the upstream
# Mbed TLS v3.6.4 build. Mirrors component/amebad/mbedtls_port (same `mbedtls_port` lib +
# `mbedtls_cfg` INTERFACE); only the MBEDTLS_*_ALT selections, the alt source files, and the
# HW dep lib differ. The upstream mbedcrypto/mbedx509/mbedtls targets are built by
# component/mbedtls_config; this dir supplies the Bouffalo HACC/PKA alt impls + config wiring.
# (The cfg -> upstream attach + the mbedtls_port -> image link live in the mbedtls block of
# component/bouffalo_sdk/CMakeLists.txt, because the HACC fork-alts borrow mbedcrypto's private
# headers -> a genex edge that must be closed one-way in the parent.)

# Config surface attached PUBLIC onto the upstream mbedcrypto/mbedx509 targets: the MBEDTLS_*_ALT
# selections whose HACC/PKA impls live in THIS dir. MBEDTLS_USER_CONFIG_FILE + MBEDTLS_THREADING_ALT
# resolve the shared config in component/mbedtls_config.
#   - MBEDTLS_ENTROPY_HARDWARE_ALT -> entropy_poll_alt.c (SEC TRNG)
#   - MBEDTLS_SHA1/256/512_ALT + MBEDTLS_AES_ALT + MBEDTLS_ECP_ALT -> HACC / PKA (this dir)
# The include dir carries the *_alt.h context structs (upstream <mbedtls/aes.h> #include "aes_alt.h").
add_library(mbedtls_cfg INTERFACE)
target_compile_definitions(mbedtls_cfg INTERFACE
    MBEDTLS_USER_CONFIG_FILE="mbedtls_user_config.h"   # shared config (mbedtls_config): the deltas
    MBEDTLS_THREADING_ALT            # threading_alt.h + mbedtls_threading_alt.c (mbedtls_config)
    MBEDTLS_ENTROPY_HARDWARE_ALT)    # mbedtls_hardware_poll(): entropy_poll_alt.c

# HACC/PKA hardware crypto ALTs. EXPERIMENT: disabled on bl618dg to test whether the
# shared Sec_Eng engine corrupts the WPA supplicant's 4-way MIC once the AWS/mbedTLS
# stack also starts using it.
# ECP/PKA stays on everywhere: it accelerates TLS public-key math only, which the
# WPA supplicant does not touch on WPA2.
target_compile_definitions(mbedtls_cfg INTERFACE MBEDTLS_ECP_ALT)

# SHA/AES ALTs are the ones the WPA supplicant shares with TLS (PBKDF2-HMAC-SHA1 for
# the PMK, AES for CCMP). On bl618dg, once the AWS/mbedTLS stack also drives the one
# Sec_Eng engine, the 4-way MIC comes out wrong and the AP reports WRONG_KEY — so run
# those in software here. See the bl616 HW-AES bus-contention bug for the same class.
if(NOT "${BOUFFALO_CHIP}" STREQUAL "bl618dg")
    target_compile_definitions(mbedtls_cfg INTERFACE
        MBEDTLS_SHA1_ALT
        MBEDTLS_SHA256_ALT
        MBEDTLS_SHA512_ALT
        MBEDTLS_AES_ALT)
endif()
target_include_directories(mbedtls_cfg SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}")                 # timing_alt.h / aes_alt.h / sha*_alt.h / ecp_alt.h / gcm_alt.h
target_link_libraries(mbedtls_cfg INTERFACE mbedtls_config_headers)  # shared mbedtls_user_config.h + threading_alt.h

# Optional HW AES-GCM (BOUFFALO_MBEDTLS_HW_GCM, from the Makefile HWGCM flag). When on, MBEDTLS_GCM_ALT
# swaps the software mbedtls_gcm module for gcm_alt.c (BL602 Sec_Eng HW AES-CTR, chunked, + software
# GHASH -- the RAM-frugal path that fits bl602's tight heap; the whole-record HW GMAC needs DMA scratch
# that OOMs the picotls handshake). Upstream gcm.c is `#if !defined(MBEDTLS_GCM_ALT)`-guarded so it goes
# inert (no exclusion needed), and the picotls transport's one-shot AEAD coalesce shim auto-activates.
if(BOUFFALO_MBEDTLS_HW_GCM)
    target_compile_definitions(mbedtls_cfg INTERFACE MBEDTLS_GCM_ALT)
endif()

# Runtime glue lib (`mbedtls_port`). The HACC/PKA alt impls are compiled unconditionally (each is
# #if-guarded by its own MBEDTLS_*_ALT internally). bignum is the odd one out: upstream
# library/bignum.c has no whole-module _ALT hook, so we ship a fork (bignum.c) that provides the
# mbedtls_mpi_* API + bignum_alt.c (HW mbedtls_mpi_exp_mod on the PKA) + bignum_ext.c (PKA helpers);
# the umbrella EXCLUDES upstream's bignum.c from mbedcrypto. entropy_poll_alt/alt_timing/
# mbedtls_platform_alt back the entropy/timing hooks on bflb_lhal.
add_library(mbedtls_port STATIC
    alt_timing.c
    entropy_poll_alt.c
    mbedtls_platform_alt.c
    bignum.c)   # upstream bignum fork (the mbedtls_mpi_* API) — NOT a HW ALT, always needed

# HACC/PKA ALT implementations — must be compiled only when the matching
# MBEDTLS_*_ALT macros above are defined (they reach into upstream's *_alt.h
# context structs, which upstream only emits under those macros).
target_sources(mbedtls_port PRIVATE
    ecp_alt.c
    ecp_curves_alt.c
    bignum_alt.c     # PKA mbedtls_mpi_exp_mod
    bignum_ext.c
    gcm_alt.c)       # self-guarded by MBEDTLS_GCM_ALT

if(NOT "${BOUFFALO_CHIP}" STREQUAL "bl618dg")
    target_sources(mbedtls_port PRIVATE
        aes_alt.c
        sha1_alt.c
        sha256_alt.c
        sha512_alt.c)
endif()   # HW AES-GCM (self-guarded by MBEDTLS_GCM_ALT; inert unless BOUFFALO_MBEDTLS_HW_GCM)
# Own dir (*_alt.h) + PUBLIC-borrow mbedcrypto's INTERFACE includes (mbedtls/*) and PRIVATE-borrow
# its SOURCE_DIR for the upstream-private headers our forks reach into (common.h, ecp_invasive.h,
# bignum_core.h). SOURCE_DIR (a fixed string) keeps the edge one-way — the parent feeds THIS dir
# back into mbedcrypto, so borrowing mbedcrypto's INTERFACE includes here would close a genex cycle.
target_include_directories(mbedtls_port PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "$<TARGET_PROPERTY:mbedcrypto,INTERFACE_INCLUDE_DIRECTORIES>")
target_include_directories(mbedtls_port PRIVATE
    "$<TARGET_PROPERTY:mbedcrypto,SOURCE_DIR>")
target_link_libraries(mbedtls_port PUBLIC bflb_lhal mbedtls_cfg)
