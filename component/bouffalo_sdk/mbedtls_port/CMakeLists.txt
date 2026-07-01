# component/bouffalo_sdk/mbedtls_port — Bouffalo's hardware-related mbedtls glue.
# Named `mbedtls_port` to match component/amebad/mbedtls_port, so a project links the
# same target regardless of platform (only one of BOUFFALO_CHIP / AMEBAD_CHIP is ever
# configured at a time).
#
# Two kinds of pieces, all backed by on-chip peripherals via bflb_lhal:
#   Platform hooks (always compiled):
#     entropy_poll_alt.c      mbedtls_hardware_poll() <- bflb_sec_trng (HW TRNG)
#     alt_timing.c            mbedtls_timing_*        <- bflb_mtimer
#     mbedtls_platform_alt.c  mbedtls_ms_time()       <- bflb_mtimer (MBEDTLS_PLATFORM_MS_TIME_ALT)
#   HACC / PKA hardware crypto ALTs (always compiled, merged from the former
#   bflb_mbedtls_hw_acc): aes / sha1 / sha256 / sha512 / ecp / bignum.
#
# The upstream MBEDTLS_*_ALT / MBEDTLS_ENTROPY_HARDWARE_ALT / MBEDTLS_PLATFORM_MS_TIME_ALT
# macros are carried by bflb_mbedtls_config (config-tls-generic.h, now defined
# unconditionally) — upstream is built unchanged and we layer on via those macros plus this
# lib's strong overrides.
#
# The former net_sockets.c (lwip-backed mbedtls_net_*, MBEDTLS_NET_C is off / no callers)
# was dropped — this lib has no lwip dependency.
#
# HACC / PKA hardware crypto, wired through the upstream MBEDTLS_*_ALT mechanism.
# How it works (Mbed TLS up to v3.6.4): config (bflb_mbedtls_config) defines
# MBEDTLS_<PRIMITIVE>_ALT unconditionally, upstream's <mbedtls/aes.h> then skips its own
# context and `#include "aes_alt.h"` (so this dir must be on the include path), and
# library/aes.c wraps its default impl in !defined(MBEDTLS_AES_ALT) so the alt is the sole
# provider — no collision. All alt sources are compiled unconditionally (no per-primitive
# toggles).
#
# bignum is the odd one out: upstream library/bignum.c has NO whole-module _ALT hook, so
# HW modexp (mbedtls_mpi_exp_mod on the PKA, bignum_alt.c) can't be selected by a macro.
# Instead we ship a fork of upstream's bignum.c (bignum.c here) that renames the software
# mbedtls_mpi_exp_mod -> mbedtls_mpi_exp_mod_original (kept for the large-operand fallback),
# and the umbrella CMakeLists EXCLUDES upstream's library/bignum.c from mbedcrypto so this
# fork provides the mbedtls_mpi_* API. bignum_alt.c then defines the real (HW) exp_mod and
# bignum_ext.c the PKA helper layer. bignum_core.c/bignum_mod*.c stay upstream.
add_library(mbedtls_port STATIC
    alt_timing.c
    entropy_poll_alt.c
    mbedtls_platform_alt.c
    aes_alt.c
    sha1_alt.c
    sha256_alt.c
    sha512_alt.c
    ecp_alt.c
    ecp_curves_alt.c
    bignum.c
    bignum_alt.c
    bignum_ext.c
)

# Own dir carries timing_alt.h + the *_alt.h context structs (upstream headers #include
# "aes_alt.h" et al by bare name when MBEDTLS_*_ALT is on). PUBLIC-borrow mbedcrypto's
# INTERFACE include set so consumers see mbedtls/* transitively, and PRIVATE-borrow its
# SOURCE_DIR (component/mbedtls_config/mbedtls/library/) for the upstream-private headers
# our impls reach into (common.h, ecp_invasive.h, bignum_core.h). Using SOURCE_DIR (a fixed
# string) rather than mbedcrypto's INTERFACE_INCLUDE_DIRECTORIES keeps the edge one-way: the
# mbedtls block in component/bouffalo_sdk/CMakeLists.txt feeds THIS dir back into mbedcrypto
# via SOURCE_DIR, so borrowing its INTERFACE includes here would close a genex cycle.
target_include_directories(mbedtls_port PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "$<TARGET_PROPERTY:mbedcrypto,INTERFACE_INCLUDE_DIRECTORIES>"
)
target_include_directories(mbedtls_port PRIVATE
    "$<TARGET_PROPERTY:mbedcrypto,SOURCE_DIR>"
)

# Real-target deps. bflb_lhal — sec_trng + mtimer + SEC AES/SHA + PKA drivers. config —
# MBEDTLS_CONFIG_FILE + the MBEDTLS_*_ALT macros.
target_link_libraries(mbedtls_port PUBLIC
    bflb_lhal
    bflb_mbedtls_config
)
