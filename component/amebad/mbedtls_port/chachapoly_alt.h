/*
 * chachapoly_alt.h — AmebaD hardware ChaCha20-Poly1305 context, replacing the
 * mbedTLS software module when MBEDTLS_CHACHAPOLY_ALT is defined. The crypto runs
 * on the RTL8721D IPSEC engine. Unlike AES-GCM, there is NO one-shot ROM wrapper
 * (rtl_crypto_chacha_poly1305_* are SDK-source only), so chachapoly_alt.c drives
 * the ROM generic-cipher path (CRYPTO_CipherInit/EncryptAD/DecryptAD + the ROM
 * crypto_engine) inline — no SDK source vendored. See chachapoly_alt.c.
 *
 * Scope: ChaCha20-Poly1305 is fixed 256-bit key / 96-bit nonce / 128-bit tag.
 * One-shot crypt_and_tag/auth_decrypt are full HW (the PSA AEAD hot path);
 * streaming is single-update (the engine is one-shot). Buffers must be SRAM (the
 * IPSEC DMA cannot read XIP flash). Engine shared with WiFi -> RT_DEV_LOCK_CRYPTO.
 */
#ifndef MBEDTLS_CHACHAPOLY_ALT_H
#define MBEDTLS_CHACHAPOLY_ALT_H

#include <stddef.h>

typedef struct mbedtls_chachapoly_context {
    unsigned char key[32];     /* 256-bit ChaCha20 key                  */
    unsigned char nonce[12];   /* 96-bit nonce (streaming path)         */
    unsigned char aad[64];     /* buffered AAD for the streaming path   */
    size_t        aad_len;
    unsigned char tag[16];     /* tag from the one HW op                */
    int           mode;        /* MBEDTLS_CHACHAPOLY_ENCRYPT / _DECRYPT */
    int           have_key;
    int           started;
    int           did_op;
} mbedtls_chachapoly_context;

#endif /* MBEDTLS_CHACHAPOLY_ALT_H */
