/*
 * gcm_alt.h — AmebaD hardware AES-GCM context, replacing the mbedTLS software GCM
 * module when MBEDTLS_GCM_ALT is defined. The crypto runs on the RTL8721D IPSEC
 * engine via the NS ROM rtl_crypto_aes_gcm_* (NSPE-direct works under TF-M's SAU;
 * see reference_amebad_hw_crypto / doc/amebad_crypto_mbedtls_vs_wolfssl.md).
 * Implemented in gcm_alt.c.
 *
 * Scope: 96-bit IV (TLS/QUIC) + AES-128/192/256. One-shot crypt_and_tag /
 * auth_decrypt are full HW (the PSA AEAD hot path -> psa_crypto_aead.c). Streaming
 * is single-update only (the engine is one-shot); a second update() returns
 * UNSUPPORTED — permitted for an ALT per mbedtls/gcm.h. Buffers must be in SRAM
 * (the IPSEC DMA cannot read XIP flash).
 *
 * Concurrency: the IPSEC engine is one shared resource — the WiFi driver and ROM
 * crypto use it too. gcm_alt.c serializes each HW op with the AmebaD device lock
 * RT_DEV_LOCK_CRYPTO (init+op held together), so a concurrent WiFi crypto op can't
 * clobber the engine mid-GCM.
 */
#ifndef MBEDTLS_GCM_ALT_H
#define MBEDTLS_GCM_ALT_H

#include <stddef.h>

typedef struct mbedtls_gcm_context {
    unsigned char key[32];     /* AES key bytes                         */
    unsigned int  keybits;     /* 128 / 192 / 256 (0 = no key set)      */
    /* single-update streaming state (the HW op is one-shot)            */
    unsigned char iv[12];      /* 96-bit GCM nonce                      */
    unsigned char aad[64];     /* buffered AAD for the streaming path    */
    size_t        aad_len;
    unsigned char tag[16];     /* tag produced by the one HW op          */
    int           mode;        /* MBEDTLS_GCM_ENCRYPT / _DECRYPT         */
    int           started;     /* mbedtls_gcm_starts() called            */
    int           did_op;      /* the single HW op has run               */
} mbedtls_gcm_context;

#endif /* MBEDTLS_GCM_ALT_H */
