/*
 * gcm_alt.h — BL602 AES-GCM context, replacing the mbedTLS software GCM module
 * when MBEDTLS_GCM_ALT is defined. GCM is built from the BL602 Sec_Eng AES
 * primitives + a software GHASH (implemented in gcm_alt.c), RAM-frugally:
 *
 *   - AES-CTR (HW, chunked through a small 512-byte un-cached buffer) keystream,
 *   - AES-ECB (HW, one block) for H = E(K,0) and for E(K,J0),
 *   - GHASH in SOFTWARE (table-free GF(2^128)).
 *
 * The BL602's .nocache_noinit RAM comes straight out of the TLSF heap, and the
 * TLS 1.3 handshake has only ~7 KB of headroom, so a full-record DMA buffer is
 * not affordable and a record-size cap would kill AWS's multi-KB handshake
 * records. The software GHASH removes the big GMAC message buffer, so there is NO
 * record-size cap — only the fixed 512-byte CTR piece is static un-cached scratch
 * (~1 KB total). See gcm_alt.c for the full construction.
 *
 * Scope: 96-bit IV (TLS/QUIC) + AES-128/192/256, tag 4..16 bytes.  One-shot
 * crypt_and_tag / auth_decrypt are the PSA AEAD hot path.  Streaming is
 * single-update only (each update() does the whole record); a second update()
 * returns UNSUPPORTED — permitted for an ALT per mbedtls/gcm.h.  AAD is capped at
 * 64 bytes (TLS AAD is <= 13); a larger AAD returns UNSUPPORTED.
 */
#ifndef MBEDTLS_GCM_ALT_H
#define MBEDTLS_GCM_ALT_H

#include <stddef.h>

typedef struct mbedtls_gcm_context {
    unsigned char key[32];     /* AES key bytes                          */
    unsigned int  keybits;     /* 128 / 192 / 256 (0 = no key set)       */
    /* single-update streaming state (the HW op is one-shot)             */
    unsigned char iv[12];      /* 96-bit GCM nonce                       */
    unsigned char aad[64];     /* buffered AAD for the streaming path     */
    size_t        aad_len;
    unsigned char tag[16];     /* tag produced by the one HW op           */
    int           mode;        /* MBEDTLS_GCM_ENCRYPT / _DECRYPT          */
    int           started;     /* mbedtls_gcm_starts() called             */
    int           did_op;      /* the single HW op has run                */
} mbedtls_gcm_context;

#endif /* MBEDTLS_GCM_ALT_H */
