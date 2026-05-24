# bflb_mbedtls_hw_acc — Bouffalo HACC-backed mbedtls primitives, opted
# in via the upstream MBEDTLS_*_ALT mechanism. Extracted from
# bouffalo_sdk/components/crypto/mbedtls/port/hw_acc/.
#
# How the ALT mechanism works (Mbed TLS up to v3.6.4):
#   1. The config (bflb_mbedtls_config/config-tls-generic.h) defines
#      MBEDTLS_<PRIMITIVE>_ALT for each primitive we accelerate.
#   2. Upstream's <mbedtls/aes.h> sees MBEDTLS_AES_ALT and SKIPS its own
#      `struct mbedtls_aes_context` — instead `#include "aes_alt.h"` is
#      pulled in (relative path, so this lib must be on the include line)
#      and we provide the struct + all the public mbedtls_aes_* funcs in
#      aes_alt.c.
#   3. Upstream's library/aes.c then wraps every default impl in
#      `#if !defined(MBEDTLS_AES_ALT)`, so the upstream impl drops out
#      and nothing collides at link time.
#
# This contract was BROKEN in Mbed TLS 3.6.5+ — the alt wrappers were
# removed when the crypto primitives moved into tf-psa-crypto/. That's
# why cmake/mbedtls.cmake pins at v3.6.4.
#
# Per-primitive opt-in via BFLB_MBEDTLS_HW_*:
#   AES, SHA1, SHA256, SHA512, BIGNUM, ECC
# Default ALL on (matches the project's proj.conf, which sets every
# CONFIG_MBEDTLS_*_USE_HW=y). The corresponding MBEDTLS_*_ALT macros are
# carried by bflb_mbedtls_config to keep upstream untouched.

option(BFLB_MBEDTLS_HW_AES    "HW AES via HACC"             ON)
option(BFLB_MBEDTLS_HW_SHA1   "HW SHA-1 via HACC"           ON)
option(BFLB_MBEDTLS_HW_SHA256 "HW SHA-256 via HACC"         ON)
option(BFLB_MBEDTLS_HW_SHA512 "HW SHA-512 via HACC"         ON)
option(BFLB_MBEDTLS_HW_BIGNUM "HW BIGNUM (modular) via PKA" ON)
option(BFLB_MBEDTLS_HW_ECC    "HW ECP / curves via PKA"     ON)

if(NOT DEFINED AT_PROJECT_ROOT)
    message(FATAL_ERROR "bflb_mbedtls_hw_acc: AT_PROJECT_ROOT must be set "
                        "so we can reach component/mbedtls/library private headers.")
endif()

add_library(bflb_mbedtls_hw_acc STATIC)

if(BFLB_MBEDTLS_HW_AES)
    target_sources(bflb_mbedtls_hw_acc PRIVATE aes_alt.c)
endif()
if(BFLB_MBEDTLS_HW_SHA1)
    target_sources(bflb_mbedtls_hw_acc PRIVATE sha1_alt.c)
endif()
if(BFLB_MBEDTLS_HW_SHA256)
    target_sources(bflb_mbedtls_hw_acc PRIVATE sha256_alt.c)
endif()
if(BFLB_MBEDTLS_HW_SHA512)
    target_sources(bflb_mbedtls_hw_acc PRIVATE sha512_alt.c)
endif()
# bignum + ECC share bignum_ext.c (extension hooks into the PKA layer).
if(BFLB_MBEDTLS_HW_BIGNUM)
    target_sources(bflb_mbedtls_hw_acc PRIVATE bignum_alt.c)
endif()
if(BFLB_MBEDTLS_HW_ECC)
    target_sources(bflb_mbedtls_hw_acc PRIVATE ecp_alt.c ecp_curves_alt.c)
endif()
if(BFLB_MBEDTLS_HW_BIGNUM OR BFLB_MBEDTLS_HW_ECC)
    target_sources(bflb_mbedtls_hw_acc PRIVATE bignum_ext.c)
endif()

# *_alt.h must be visible to upstream's <mbedtls/aes.h> et al when the
# corresponding MBEDTLS_*_ALT is on — those headers do `#include "aes_alt.h"`
# expecting it to be on the global include path.
target_include_directories(bflb_mbedtls_hw_acc PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# alt .c files reach into upstream's library/ private headers
# (common.h, ecp_invasive.h, bignum_core.h, …) AND its public include/
# (mbedtls/aes.h, mbedtls/bignum.h, mbedtls/build_info.h, …).
# Reference them directly via the submodule path — no target_link to
# mbedcrypto, because that target may not exist yet when this dir is
# processed (it gets created by cmake/mbedtls.cmake which runs later),
# and because we're a SIBLING to mbedcrypto in the final archive, not
# a consumer of its symbols.
target_include_directories(bflb_mbedtls_hw_acc PRIVATE
    "${AT_PROJECT_ROOT}/component/mbedtls/library"
)
target_include_directories(bflb_mbedtls_hw_acc PUBLIC
    "${AT_PROJECT_ROOT}/component/mbedtls/include"
)

# Deps: bflb_lhal for the SEC AES / SHA / PKA peripheral drivers, plus
# the config target (for MBEDTLS_*_ALT macros + config header).
target_link_libraries(bflb_mbedtls_hw_acc PUBLIC
    bflb_lhal
    bflb_mbedtls_config
)
