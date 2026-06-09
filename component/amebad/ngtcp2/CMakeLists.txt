# amebad_ngtcp2 — QUIC transport (ngtcp2, from component/thirdparty/ngtcp2 @
# v1.23.0) built for KM4 (Cortex-M33) as a bare-metal static lib, plus the
# ngtcp2 crypto helper bound to the in-tree wolfSSL TLS 1.3 + QUIC layer
# (amebad_wolfssl built with AMEBAD_WOLFSSL_TLS).
#
# Portability decisions for the no-OS target (see doc):
#   - NGTCP2_USE_GENERIC_SOCKADDR: ngtcp2 uses its OWN sockaddr structs instead
#     of <sys/socket.h>/<netinet/in.h> (newlib here ships neither). The QUIC
#     test code converts between FreeRTOS-Plus-TCP's freertos_sockaddr and
#     ngtcp2_sockaddr_in. PUBLIC so consumers see the same ngtcp2.h layout.
#   - HAVE_CONFIG_H is NOT defined: ngtcp2_net.h falls back to portable C byte
#     swaps (target is little-endian) and ngtcp2_unreachable.c collapses to a
#     bare abort() (no stdio/malloc/write — HAVE_UNISTD_H stays undefined).
#   - version.h is hand-supplied under generated/ (no configure step).
#   - default ngtcp2_mem -> malloc/free/calloc/realloc, and the bare htonl/htons
#     ngtcp2_net.h needs, both provided by amebad_syscalls (syscalls/libc_compat.c)
#     and resolved at the final KM4 image link.
#     over the FreeRTOS heap (this image has no newlib _sbrk).
#
# KM4-only, EXCLUDE_FROM_ALL until app_km4 links it. Gated by the caller:
# add_subdirectory'd only when AMEBAD_ENABLE_QUIC is ON.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

set(_ng "${CMAKE_SOURCE_DIR}/component/thirdparty/ngtcp2")

# ---- core transport (lib/*.c) ----
add_library(amebad_ngtcp2 STATIC EXCLUDE_FROM_ALL
    "${_ng}/lib/ngtcp2_acktr.c"
    "${_ng}/lib/ngtcp2_addr.c"
    "${_ng}/lib/ngtcp2_balloc.c"
    "${_ng}/lib/ngtcp2_bbr.c"
    "${_ng}/lib/ngtcp2_buf.c"
    "${_ng}/lib/ngtcp2_callbacks.c"
    "${_ng}/lib/ngtcp2_cc.c"
    "${_ng}/lib/ngtcp2_cid.c"
    "${_ng}/lib/ngtcp2_conn.c"
    "${_ng}/lib/ngtcp2_conn_info.c"
    "${_ng}/lib/ngtcp2_conv.c"
    "${_ng}/lib/ngtcp2_crypto.c"
    "${_ng}/lib/ngtcp2_dcidtr.c"
    "${_ng}/lib/ngtcp2_err.c"
    "${_ng}/lib/ngtcp2_fmt.c"
    "${_ng}/lib/ngtcp2_frame_chain.c"
    "${_ng}/lib/ngtcp2_gaptr.c"
    "${_ng}/lib/ngtcp2_idtr.c"
    "${_ng}/lib/ngtcp2_ksl.c"
    "${_ng}/lib/ngtcp2_log.c"
    "${_ng}/lib/ngtcp2_map.c"
    "${_ng}/lib/ngtcp2_mem.c"
    "${_ng}/lib/ngtcp2_objalloc.c"
    "${_ng}/lib/ngtcp2_opl.c"
    "${_ng}/lib/ngtcp2_path.c"
    "${_ng}/lib/ngtcp2_pcg.c"
    "${_ng}/lib/ngtcp2_pkt.c"
    "${_ng}/lib/ngtcp2_pmtud.c"
    "${_ng}/lib/ngtcp2_ppe.c"
    "${_ng}/lib/ngtcp2_pq.c"
    "${_ng}/lib/ngtcp2_pv.c"
    "${_ng}/lib/ngtcp2_qlog.c"
    "${_ng}/lib/ngtcp2_range.c"
    "${_ng}/lib/ngtcp2_ratelim.c"
    "${_ng}/lib/ngtcp2_ringbuf.c"
    "${_ng}/lib/ngtcp2_rob.c"
    "${_ng}/lib/ngtcp2_rst.c"
    "${_ng}/lib/ngtcp2_rtb.c"
    "${_ng}/lib/ngtcp2_settings.c"
    "${_ng}/lib/ngtcp2_str.c"
    "${_ng}/lib/ngtcp2_strm.c"
    "${_ng}/lib/ngtcp2_transport_params.c"
    "${_ng}/lib/ngtcp2_unreachable.c"
    "${_ng}/lib/ngtcp2_vec.c"
    "${_ng}/lib/ngtcp2_version.c"
    "${_ng}/lib/ngtcp2_window_filter.c"
)

target_include_directories(amebad_ngtcp2 PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/generated"   # -> <ngtcp2/version.h>
    "${_ng}/lib/includes"                     # -> <ngtcp2/ngtcp2.h>
)
target_include_directories(amebad_ngtcp2 PRIVATE
    "${_ng}/lib"                              # internal ngtcp2_*.h
)

target_compile_definitions(amebad_ngtcp2 PUBLIC
    NGTCP2_USE_GENERIC_SOCKADDR   # use ngtcp2's own sockaddr structs (no sys/socket.h)
    NGTCP2_STATICLIB              # no dllimport/visibility decoration
    NGTCP2_AF_INET=2              # sa_family values for the generic sockaddr
    NGTCP2_AF_INET6=10            # (Linux convention; only stored/compared internally)
)

# Vendor sources: silence warnings. (malloc/htonl now come from amebad_syscalls,
# resolved at the final image link — ngtcp2 no longer needs a FreeRTOS dep here.)
target_compile_options(amebad_ngtcp2 PRIVATE -w)

# ---- crypto helper bound to wolfSSL (crypto/shared.c + crypto/wolfssl) ----
add_library(amebad_ngtcp2_crypto STATIC EXCLUDE_FROM_ALL
    "${_ng}/crypto/shared.c"
    "${_ng}/crypto/wolfssl/wolfssl.c"
)

target_include_directories(amebad_ngtcp2_crypto PUBLIC
    "${_ng}/crypto/includes"                       # -> <ngtcp2/ngtcp2_crypto.h>, ngtcp2_crypto_wolfssl.h
    "${CMAKE_CURRENT_SOURCE_DIR}/wolfssl_compat"   # empty <wolfssl/options.h> shim (no configure step)
)
target_include_directories(amebad_ngtcp2_crypto PRIVATE
    "${_ng}/crypto"                 # shared.h
    "${_ng}/lib"                    # ngtcp2_macro.h, ngtcp2_net.h
)

# Public deps: ngtcp2 core (ngtcp2.h) + full wolfSSL TLS/QUIC (ssl.h, quic.h).
target_link_libraries(amebad_ngtcp2_crypto PUBLIC amebad_ngtcp2 amebad_wolfssl)
target_compile_options(amebad_ngtcp2_crypto PRIVATE -w)
