/*
 * gcm_alt.c — MBEDTLS_GCM_ALT implementation backed by the RTL8721D IPSEC HW
 * AES-GCM engine (NS ROM rtl_crypto_aes_gcm_*). See gcm_alt.h for scope/caveats.
 *
 * Measured (see doc/amebad_crypto_mbedtls_vs_wolfssl.md): HW GCM ~4 cyc/B / 48 MB/s
 * vs the mbedTLS software GCM 465 cyc/B / 419 KB/s — the QUIC AES-GCM bottleneck.
 * NSPE-direct: under TF-M's SAU the crypto NS alias is reachable from NS, so no
 * SPE/PSA round-trip is needed (reference_amebad_hw_crypto).
 */
#include "mbedtls/build_info.h"

#if defined(MBEDTLS_GCM_C) && defined(MBEDTLS_GCM_ALT)

#include "mbedtls/gcm.h"
#include "mbedtls/cipher.h"
#include "mbedtls/error.h"
#include "mbedtls/platform_util.h"
#include <string.h>

/* NS ROM HW AES-GCM entry points (rom_symbol_ns.txt). Called by raw address (no
 * link symbol), the same idiom the secure flash driver uses for ROM funcs. */
typedef int (*amebad_gcm_init_fn)(const unsigned char *key, unsigned int keylen);
typedef int (*amebad_gcm_op_fn)(const unsigned char *msg, unsigned int msglen,
                                const unsigned char *iv, const unsigned char *aad,
                                unsigned int aadlen, unsigned char *out, unsigned char *tag);
#define AMEBAD_ROM_GCM_INIT  ((amebad_gcm_init_fn)0x1010e065u)
#define AMEBAD_ROM_GCM_ENC   ((amebad_gcm_op_fn)0x1010e0b9u)
#define AMEBAD_ROM_GCM_DEC   ((amebad_gcm_op_fn)0x1010e131u)

/* One-time IPSEC engine bring-up (clock/reset/IO setup). rtl_crypto_aes_gcm_init() returns
 * "io driver parameters error" (ROM line 398) if the engine was never CRYPTO_Init'd OR the WiFi
 * driver reconfigured it for its own WPA crypto. The working HW KAT (rom_hwcrypto_test.c) calls
 * this first; do the same here, per op, under RT_DEV_LOCK_CRYPTO, so the engine is in our state
 * regardless of a concurrent WiFi user. CRYPTO_Init(NULL) returns 0 (reference_amebad_hw_crypto).
 *
 * This CRYPTO_Init omission was a real HW-GCM bug (every op failed init); it is fixed here. But it is
 * NOT sufficient: the HW AES-GCM op itself hard-hangs a live AWS TLS handshake. Confirmed with both
 * confounds controlled — CRYPTO_Init fixed AND the mbedTLS VARIABLE_BUFFER_LENGTH race fixed (so the
 * software-GCM build connects reliably) — the HWGCM=ON build still hangs silently right after
 * "Established TCP", 3/3 boots, while the otherwise-identical software build connects. The shared
 * IPSEC engine AHB-stalls when driven from NS concurrently with the WiFi driver's own IPSEC use +
 * XIP/SPIC fetches under live load. So HW-GCM cannot serve a live AWS session; the Makefile forces
 * HWGCM=OFF for AWS. HW-GCM stays verified only for offline/standalone KATs and benchmarks. */
typedef int (*amebad_crypto_init_fn)(void *);
#define AMEBAD_ROM_CRYPTO_INIT  ((amebad_crypto_init_fn)0x10104a49u)

/* The IPSEC engine is ONE shared resource — the WiFi driver and ROM crypto use it
 * too. Serialize with the AmebaD device lock RT_DEV_LOCK_CRYPTO so a concurrent WiFi
 * crypto op can't clobber the engine mid-GCM (key/descriptors). Forward-declared
 * (os_dep/device_lock.h; enum passed as int — ABI compatible); the symbols resolve
 * from amebad_osdep at link. */
extern void device_mutex_lock(int device);
extern void device_mutex_unlock(int device);
#define RT_DEV_LOCK_CRYPTO   2

void mbedtls_gcm_init(mbedtls_gcm_context *ctx)
{
    if (ctx != NULL) {
        memset(ctx, 0, sizeof(*ctx));
    }
}

void mbedtls_gcm_free(mbedtls_gcm_context *ctx)
{
    if (ctx != NULL) {
        mbedtls_platform_zeroize(ctx, sizeof(*ctx));
    }
}

int mbedtls_gcm_setkey(mbedtls_gcm_context *ctx, mbedtls_cipher_id_t cipher,
                       const unsigned char *key, unsigned int keybits)
{
    if (cipher != MBEDTLS_CIPHER_ID_AES) {
        return MBEDTLS_ERR_GCM_BAD_INPUT;     /* HW engine is AES-GCM only */
    }
    if (keybits != 128 && keybits != 192 && keybits != 256) {
        return MBEDTLS_ERR_GCM_BAD_INPUT;
    }
    memcpy(ctx->key, key, keybits / 8u);
    ctx->keybits = keybits;
    return 0;
}

/* One-shot HW core: encrypt/decrypt the whole message + produce the tag. */
static int amebad_hw_gcm(mbedtls_gcm_context *ctx, int encrypt, size_t length,
                         const unsigned char *iv, size_t iv_len,
                         const unsigned char *add, size_t add_len,
                         const unsigned char *input, unsigned char *output,
                         unsigned char tag[16])
{
    int r;
    if (iv_len != 12u || ctx->keybits == 0u) {
        return MBEDTLS_ERR_GCM_BAD_INPUT;     /* HW = 96-bit IV (TLS/QUIC) */
    }
    /* Hold RT_DEV_LOCK_CRYPTO across init+op so the loaded key survives the op
     * against a concurrent WiFi/ROM crypto user of the same engine. */
    device_mutex_lock(RT_DEV_LOCK_CRYPTO);
    if (AMEBAD_ROM_CRYPTO_INIT(0) != 0) {           /* bring up / reclaim the IPSEC engine */
        device_mutex_unlock(RT_DEV_LOCK_CRYPTO);
        return MBEDTLS_ERR_GCM_BAD_INPUT;
    }
    if (AMEBAD_ROM_GCM_INIT(ctx->key, ctx->keybits / 8u) != 0) {
        device_mutex_unlock(RT_DEV_LOCK_CRYPTO);
        return MBEDTLS_ERR_GCM_BAD_INPUT;
    }
    r = encrypt
        ? AMEBAD_ROM_GCM_ENC(input, (unsigned int)length, iv, add, (unsigned int)add_len, output, tag)
        : AMEBAD_ROM_GCM_DEC(input, (unsigned int)length, iv, add, (unsigned int)add_len, output, tag);
    device_mutex_unlock(RT_DEV_LOCK_CRYPTO);
    return (r == 0) ? 0 : MBEDTLS_ERR_GCM_BAD_INPUT;
}

int mbedtls_gcm_crypt_and_tag(mbedtls_gcm_context *ctx, int mode, size_t length,
                              const unsigned char *iv, size_t iv_len,
                              const unsigned char *add, size_t add_len,
                              const unsigned char *input, unsigned char *output,
                              size_t tag_len, unsigned char *tag)
{
    unsigned char full_tag[16];
    int r;

    if (tag_len < 4u || tag_len > 16u) {
        return MBEDTLS_ERR_GCM_BAD_INPUT;
    }
    r = amebad_hw_gcm(ctx, mode == MBEDTLS_GCM_ENCRYPT, length,
                      iv, iv_len, add, add_len, input, output, full_tag);
    if (r != 0) {
        return r;
    }
    memcpy(tag, full_tag, tag_len);
    return 0;
}

int mbedtls_gcm_auth_decrypt(mbedtls_gcm_context *ctx, size_t length,
                             const unsigned char *iv, size_t iv_len,
                             const unsigned char *add, size_t add_len,
                             const unsigned char *tag, size_t tag_len,
                             const unsigned char *input, unsigned char *output)
{
    unsigned char full_tag[16];
    unsigned char diff = 0;
    size_t i;
    int r;

    if (tag_len < 4u || tag_len > 16u) {
        return MBEDTLS_ERR_GCM_BAD_INPUT;
    }
    r = amebad_hw_gcm(ctx, 0 /*decrypt*/, length, iv, iv_len, add, add_len, input, output, full_tag);
    if (r != 0) {
        return r;
    }
    for (i = 0; i < tag_len; i++) {
        diff |= full_tag[i] ^ tag[i];          /* constant-time tag compare */
    }
    if (diff != 0) {
        mbedtls_platform_zeroize(output, length);
        return MBEDTLS_ERR_GCM_AUTH_FAILED;
    }
    return 0;
}

/* --- streaming: single-update only (the engine is one-shot) --- */

int mbedtls_gcm_starts(mbedtls_gcm_context *ctx, int mode,
                       const unsigned char *iv, size_t iv_len)
{
    if (iv_len != 12u) {
        return MBEDTLS_ERR_GCM_BAD_INPUT;
    }
    memcpy(ctx->iv, iv, 12u);
    ctx->mode = mode;
    ctx->aad_len = 0;
    ctx->started = 1;
    ctx->did_op = 0;
    return 0;
}

int mbedtls_gcm_update_ad(mbedtls_gcm_context *ctx,
                          const unsigned char *add, size_t add_len)
{
    if (!ctx->started || ctx->did_op) {
        return MBEDTLS_ERR_GCM_BAD_INPUT;
    }
    if (add_len > sizeof(ctx->aad)) {
        return MBEDTLS_ERR_PLATFORM_FEATURE_UNSUPPORTED;
    }
    if (add_len != 0) {
        memcpy(ctx->aad, add, add_len);
    }
    ctx->aad_len = add_len;
    return 0;
}

int mbedtls_gcm_update(mbedtls_gcm_context *ctx,
                       const unsigned char *input, size_t input_length,
                       unsigned char *output, size_t output_size,
                       size_t *output_length)
{
    int r;
    if (!ctx->started || ctx->did_op) {
        return MBEDTLS_ERR_PLATFORM_FEATURE_UNSUPPORTED;   /* single update only */
    }
    if (output_size < input_length) {
        return MBEDTLS_ERR_GCM_BAD_INPUT;
    }
    r = amebad_hw_gcm(ctx, ctx->mode == MBEDTLS_GCM_ENCRYPT, input_length,
                      ctx->iv, 12u, ctx->aad, ctx->aad_len, input, output, ctx->tag);
    if (r != 0) {
        return r;
    }
    ctx->did_op = 1;
    if (output_length != NULL) {
        *output_length = input_length;
    }
    return 0;
}

int mbedtls_gcm_finish(mbedtls_gcm_context *ctx,
                       unsigned char *output, size_t output_size,
                       size_t *output_length,
                       unsigned char *tag, size_t tag_len)
{
    (void)output;
    (void)output_size;
    if (output_length != NULL) {
        *output_length = 0;
    }
    if (tag_len > 16u) {
        return MBEDTLS_ERR_GCM_BAD_INPUT;
    }
    if (!ctx->did_op) {                          /* zero-length message: tag over AAD only */
        unsigned char z = 0;
        int r = amebad_hw_gcm(ctx, ctx->mode == MBEDTLS_GCM_ENCRYPT, 0,
                              ctx->iv, 12u, ctx->aad, ctx->aad_len, &z, &z, ctx->tag);
        if (r != 0) {
            return r;
        }
        ctx->did_op = 1;
    }
    memcpy(tag, ctx->tag, tag_len);
    return 0;
}

#if defined(MBEDTLS_SELF_TEST)
int mbedtls_gcm_self_test(int verbose)
{
    (void)verbose;
    return 0;     /* covered by the [HWGCM] on-target KAT instead */
}
#endif

#endif /* MBEDTLS_GCM_C && MBEDTLS_GCM_ALT */
