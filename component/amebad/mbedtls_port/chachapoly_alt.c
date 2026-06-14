/*
 * chachapoly_alt.c — MBEDTLS_CHACHAPOLY_ALT on the RTL8721D IPSEC engine.
 *
 * rtl_crypto_chacha_poly1305_* are NOT in ROM (only the SDK source has them), so we
 * replicate the SDK wrapper (fwlib/crypto/rtl8721dhp_chacha_poly1305.c) inline using
 * the ROM generic-cipher functions + the ROM crypto_engine global — no SDK source.
 * IV = LE(counter=1)[4] || nonce[12] (exactly the SDK wrapper). Engine shared with
 * WiFi -> RT_DEV_LOCK_CRYPTO. Buffers must be SRAM (IPSEC DMA can't read flash).
 *
 * NB: with HW AES-GCM available, the device PREFERS GCM in cipher negotiation, so
 * this is the cold path (only a ChaCha-only peer reaches it).
 */
#include "mbedtls/build_info.h"

#if defined(MBEDTLS_CHACHAPOLY_C) && defined(MBEDTLS_CHACHAPOLY_ALT)

#include "mbedtls/chachapoly.h"
#include "mbedtls/error.h"
#include "mbedtls/platform_util.h"
#include <string.h>

/* ROM generic-cipher path (rom_symbol_ns.txt). The crypto_engine global is ROM BSS
 * (NS-attributed under TF-M's SAU). CRYPTO_CipherInit asserts the engine is inited;
 * rtl_cryptoEngine_init() (=CRYPTO_Init(NULL)) guarantees it. */
typedef int (*cipher_init_fn)(void *pIE, unsigned int cipher_type,
                              const unsigned char *key, unsigned int keylen);
typedef int (*cipher_ad_fn)(void *pIE, const unsigned char *msg, unsigned int msglen,
                            const unsigned char *iv, unsigned int ivlen,
                            const unsigned char *aad, unsigned int aadlen,
                            unsigned char *out, unsigned char *tag);
typedef int (*engine_init_fn)(void);
#define AMEBAD_CRYPTO_ENGINE       ((void *)0x100003a0u)   /* &crypto_engine (ROM BSS) */
#define AMEBAD_CIPHER_CHACHA_POLY  (0x30u)                 /* CIPHER_TYPE_CHACHA_POLY1305 */
#define ROM_CipherInit   ((cipher_init_fn)0x1010f799u)
#define ROM_CipherEncAD  ((cipher_ad_fn)0x1010f7b9u)
#define ROM_CipherDecAD  ((cipher_ad_fn)0x1010f7e9u)
#define ROM_EngineInit   ((engine_init_fn)0x1010ea49u)     /* rtl_cryptoEngine_init (NS) */

/* IPSEC engine is shared with the WiFi driver — serialize (see gcm_alt.c). */
extern void device_mutex_lock(int device);
extern void device_mutex_unlock(int device);
#define RT_DEV_LOCK_CRYPTO   2

/* One-shot HW ChaCha20-Poly1305 over the whole message + tag. */
static int amebad_hw_chachapoly(int encrypt, size_t length,
                                const unsigned char nonce[12],
                                const unsigned char *aad, size_t aad_len,
                                const unsigned char key[32],
                                const unsigned char *input, unsigned char *output,
                                unsigned char tag[16])
{
    static int engine_ready;                       /* one-time engine init (under lock) */
    unsigned char iv16[16] __attribute__((aligned(4)));  /* engine needs 4B-aligned IV */
    unsigned char k[32]    __attribute__((aligned(4)));  /* ... and key                */
    int r;

    /* IV = little-endian block counter (1) || 96-bit nonce — matches the SDK wrapper. */
    iv16[0] = 1; iv16[1] = 0; iv16[2] = 0; iv16[3] = 0;
    memcpy(iv16 + 4, nonce, 12);
    memcpy(k, key, 32);

    device_mutex_lock(RT_DEV_LOCK_CRYPTO);
    if (!engine_ready) {
        ROM_EngineInit();
        engine_ready = 1;
    }
    r = ROM_CipherInit(AMEBAD_CRYPTO_ENGINE, AMEBAD_CIPHER_CHACHA_POLY, k, 32);
    if (r == 0) {
        r = encrypt
            ? ROM_CipherEncAD(AMEBAD_CRYPTO_ENGINE, input, (unsigned int)length,
                              iv16, 16, aad, (unsigned int)aad_len, output, tag)
            : ROM_CipherDecAD(AMEBAD_CRYPTO_ENGINE, input, (unsigned int)length,
                              iv16, 16, aad, (unsigned int)aad_len, output, tag);
    }
    device_mutex_unlock(RT_DEV_LOCK_CRYPTO);
    return (r == 0) ? 0 : MBEDTLS_ERR_CHACHAPOLY_BAD_STATE;
}

void mbedtls_chachapoly_init(mbedtls_chachapoly_context *ctx)
{
    if (ctx != NULL) {
        memset(ctx, 0, sizeof(*ctx));
    }
}

void mbedtls_chachapoly_free(mbedtls_chachapoly_context *ctx)
{
    if (ctx != NULL) {
        mbedtls_platform_zeroize(ctx, sizeof(*ctx));
    }
}

int mbedtls_chachapoly_setkey(mbedtls_chachapoly_context *ctx,
                              const unsigned char key[32])
{
    memcpy(ctx->key, key, 32);
    ctx->have_key = 1;
    return 0;
}

int mbedtls_chachapoly_encrypt_and_tag(mbedtls_chachapoly_context *ctx, size_t length,
                                     const unsigned char nonce[12],
                                     const unsigned char *aad, size_t aad_len,
                                     const unsigned char *input, unsigned char *output,
                                     unsigned char tag[16])
{
    if (!ctx->have_key) {
        return MBEDTLS_ERR_CHACHAPOLY_BAD_STATE;
    }
    return amebad_hw_chachapoly(1 /*encrypt*/, length, nonce, aad, aad_len,
                                ctx->key, input, output, tag);
}

int mbedtls_chachapoly_auth_decrypt(mbedtls_chachapoly_context *ctx, size_t length,
                                    const unsigned char nonce[12],
                                    const unsigned char *aad, size_t aad_len,
                                    const unsigned char tag[16],
                                    const unsigned char *input, unsigned char *output)
{
    unsigned char full_tag[16];
    unsigned char diff = 0;
    size_t i;
    int r;

    if (!ctx->have_key) {
        return MBEDTLS_ERR_CHACHAPOLY_BAD_STATE;
    }
    r = amebad_hw_chachapoly(0 /*decrypt*/, length, nonce, aad, aad_len,
                             ctx->key, input, output, full_tag);
    if (r != 0) {
        return r;
    }
    for (i = 0; i < 16u; i++) {
        diff |= full_tag[i] ^ tag[i];              /* constant-time compare */
    }
    if (diff != 0) {
        mbedtls_platform_zeroize(output, length);
        return MBEDTLS_ERR_CHACHAPOLY_AUTH_FAILED;
    }
    return 0;
}

/* --- streaming: single-update only (the engine is one-shot) --- */

int mbedtls_chachapoly_starts(mbedtls_chachapoly_context *ctx,
                              const unsigned char nonce[12],
                              mbedtls_chachapoly_mode_t mode)
{
    if (!ctx->have_key) {
        return MBEDTLS_ERR_CHACHAPOLY_BAD_STATE;
    }
    memcpy(ctx->nonce, nonce, 12);
    ctx->mode = (int)mode;
    ctx->aad_len = 0;
    ctx->started = 1;
    ctx->did_op = 0;
    return 0;
}

int mbedtls_chachapoly_update_aad(mbedtls_chachapoly_context *ctx,
                                  const unsigned char *aad, size_t aad_len)
{
    if (!ctx->started || ctx->did_op) {
        return MBEDTLS_ERR_CHACHAPOLY_BAD_STATE;
    }
    if (aad_len > sizeof(ctx->aad)) {
        return MBEDTLS_ERR_PLATFORM_FEATURE_UNSUPPORTED;
    }
    if (aad_len != 0) {
        memcpy(ctx->aad, aad, aad_len);
    }
    ctx->aad_len = aad_len;
    return 0;
}

int mbedtls_chachapoly_update(mbedtls_chachapoly_context *ctx, size_t len,
                              const unsigned char *input, unsigned char *output)
{
    int r;
    if (!ctx->started || ctx->did_op) {
        return MBEDTLS_ERR_PLATFORM_FEATURE_UNSUPPORTED;   /* single update only */
    }
    r = amebad_hw_chachapoly(ctx->mode == MBEDTLS_CHACHAPOLY_ENCRYPT, len,
                             ctx->nonce, ctx->aad, ctx->aad_len, ctx->key,
                             input, output, ctx->tag);
    if (r != 0) {
        return r;
    }
    ctx->did_op = 1;
    return 0;
}

int mbedtls_chachapoly_finish(mbedtls_chachapoly_context *ctx, unsigned char mac[16])
{
    if (!ctx->started) {
        return MBEDTLS_ERR_CHACHAPOLY_BAD_STATE;
    }
    if (!ctx->did_op) {                            /* zero-length message: tag over AAD */
        unsigned char z = 0;
        int r = amebad_hw_chachapoly(ctx->mode == MBEDTLS_CHACHAPOLY_ENCRYPT, 0,
                                     ctx->nonce, ctx->aad, ctx->aad_len, ctx->key,
                                     &z, &z, ctx->tag);
        if (r != 0) {
            return r;
        }
        ctx->did_op = 1;
    }
    memcpy(mac, ctx->tag, 16);
    return 0;
}

#endif /* MBEDTLS_CHACHAPOLY_C && MBEDTLS_CHACHAPOLY_ALT */
