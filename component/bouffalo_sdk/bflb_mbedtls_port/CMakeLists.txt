# bflb_mbedtls_port — runtime glue that fills in symbols upstream Mbed
# TLS expects from "the platform". Extracted from
# bouffalo_sdk/components/crypto/mbedtls/port/ (top-level files only;
# hw_acc/ split into bflb_mbedtls_hw_acc).
#
# Files:
#   alt_timing.c               replaces mbedtls' POSIX timing with bflb_mtimer.
#   entropy_poll_alt.c         feeds mbedtls' entropy pool from bflb_sec_trng.
#   mbedtls_platform_alt.c     mbedtls_platform_set_calloc / free / printf.
#   net_sockets.c              lwip-backed mbedtls_net_* socket API.
#   timing_alt.h               mbedtls_timing_* struct decl.
#   mbedtls_port_bouffalo_sdk.h  small umbrella header port code uses.
#
# NOT included here (intentionally):
#   bignum.c                   Bouffalo's full mbedtls_mpi_* replacement,
#                              used by the SDK when CONFIG_MBEDTLS_BIGNUM_USE_HW
#                              is on. The SDK pattern in its CMakeLists
#                              swaps it for upstream's library/bignum.c —
#                              the two define the same 50+ symbols, and
#                              keeping both under --whole-archive (which
#                              we use for legacy-equivalent .itcm_region
#                              sizing) gives multiple-definition errors.
#                              We rely on upstream's software bignum; ECP
#                              still goes through the HACC alt path in
#                              bflb_mbedtls_hw_acc.
#
# Upstream is built unchanged. We layer on top via MBEDTLS_*_ALT macros
# (set by bflb_mbedtls_config) plus this lib's weak / strong overrides.

add_library(bflb_mbedtls_port STATIC
    alt_timing.c
    entropy_poll_alt.c
    mbedtls_platform_alt.c
    net_sockets.c
)

target_include_directories(bflb_mbedtls_port PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Borrow mbedcrypto's PUBLIC include set so consumers of this port see
# mbedtls/* headers transitively, and its SOURCE_DIR (= component/mbedtls/library/)
# PRIVATE for the upstream-private bignum_core.h / bn_mul.h / common.h
# / constant_time_internal.h headers our alt impls reach into.
#
# The cycle that would normally form (cmake/mbedtls.cmake reads port's
# INTERFACE_INCLUDE_DIRECTORIES and feeds it back into mbedcrypto's
# INTERFACE includes) is broken on the other side: cmake/mbedtls.cmake
# was changed to reference port's SOURCE_DIR directly instead of its
# INTERFACE_INCLUDE_DIRECTORIES, so the propagation here is one-way.
target_include_directories(bflb_mbedtls_port PUBLIC
    "$<TARGET_PROPERTY:mbedcrypto,INTERFACE_INCLUDE_DIRECTORIES>"
)
target_include_directories(bflb_mbedtls_port PRIVATE
    "$<TARGET_PROPERTY:mbedcrypto,SOURCE_DIR>"
)

# Real-target deps. bflb_lhal — sec_trng + mtimer. lwip — net_sockets.c.
target_link_libraries(bflb_mbedtls_port PUBLIC
    bflb_lhal
    bflb_mbedtls_config
    lwipcore
)
