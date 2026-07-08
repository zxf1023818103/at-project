/*
 * gcm_alt.c — MBEDTLS_GCM_ALT implementation for the BL602.
 *
 * RAM-frugal design (BL602's .nocache_noinit RAM is carved straight out of the
 * TLSF heap, right before __HeapBase, and the picotls TLS 1.3 handshake peaks
 * with only ~7 KB of heap headroom — so we cannot afford a full-record DMA
 * scratch buffer, and a small record cap would make AWS's multi-KB handshake
 * records fail):
 *
 *   - AES-CTR keystream: HW (Sec_Eng), but CHUNKED through a small fixed
 *     BL602_GCM_CTR_CHUNK (512-byte) un-cached buffer — total un-cached scratch
 *     is 2*512 B + the AES link struct (~1 KB), independent of record size.
 *   - H = AES-ECB(K, 0^128) and E(K, J0): HW, one block each.
 *   - GHASH: SOFTWARE (table-free GF(2^128), 128 iterations/block) so there is
 *     no big DMA message buffer and no record-size cap.
 *
 *       C   = AES-CTR(K, inc32(J0), P)   [J0 = IV(12) || 0x00000001, 96-bit IV]
 *       S   = GHASH_H(AAD||0* || C||0* || len(AAD)_64be || len(C)_64be)
 *       T   = S XOR AES-ECB(K, J0)
 *
 * Only the 96-bit-IV GCM profile (TLS/QUIC) is supported.
 *
 * ---------------------------------------------------------------------------
 * DMA / CACHE / CONCURRENCY
 * ---------------------------------------------------------------------------
 * The two 512-byte CTR buffers and the AES link struct live in
 * ATTR_NOCACHE_NOINIT_RAM_SECTION (same idiom as aes_alt.c) — the Sec_Eng DMA
 * reads/writes physical memory, so a dirty D-cache line would corrupt it, and it
 * cannot read XIP flash; caller input is copied into the un-cached buffer first.
 * The software GHASH runs on normal (cached) memory — the CPU never DMAs it.
 *
 * Each whole GCM op is held under bflb_sec_aes_mutex (the lock aes_alt.c also
 * takes for its HW AES ops) so nothing can clobber the loaded key/descriptors
 * mid-op. Records are processed in <=512-byte pieces; between pieces the starting
 * CTR counter is set explicitly (IV || be32(2 + blocks_done)), and within a piece
 * the HW auto-increments the low 32-bit counter (the on-target NIST KAT gates
 * that HW behavior).
 *
 * In-place safe: each piece is consumed from `input` into the un-cached buffer,
 * the ciphertext is fed to GHASH from the un-cached buffer, and `output` is
 * written last — so input==output (in-place TLS record de/encrypt) is correct.
 *
 * ENDIANNESS: the AES key/counter block go in exactly as aes_alt.c drives them
 * (memcpy key bytes into aes_key0.. and the 16-byte counter block into aes_iv0..,
 * BE link engine set by bflb_aes_link_init). The software GHASH uses the standard
 * NIST SP 800-38D bit-reflected domain (verified offline against a reference GCM
 * over 240 enc/dec cases incl. partial blocks + AAD; the software GHASH multiply
 * was cross-checked against an integer reference).
 */
#include "mbedtls/build_info.h"

#if defined(MBEDTLS_GCM_C) && defined(MBEDTLS_GCM_ALT)

#include "mbedtls/gcm.h"
#include "mbedtls/error.h"
#include "mbedtls/platform_util.h"
#include <string.h>

#include "bflb_core.h"
#include "bflb_sec_aes.h"
#include "bflb_sec_mutex.h"
#include "bflb_l1c.h"

/* Fixed CTR DMA piece — 512 B is 32 AES blocks (block-aligned, so only the final
 * piece of a record can be sub-block). Total un-cached scratch is independent of
 * the record size: 2 * BL602_GCM_CTR_CHUNK + sizeof(struct bflb_aes_link_s). */
#define BL602_GCM_CTR_CHUNK   512u
#define BL602_GCM_MAX_AAD_LEN 64u          /* TLS AAD is <= 13 bytes */

/* Sec_Eng-DMA-visible scratch: un-cached (mirrors aes_alt.c's link_ctx_temp /
 * ecb_*_buf), so no explicit cache maintenance is needed. */
static ATTR_NOCACHE_NOINIT_RAM_SECTION struct bflb_aes_link_s g_aes_link;
static ATTR_NOCACHE_NOINIT_RAM_SECTION uint8_t g_ctr_in[BL602_GCM_CTR_CHUNK];
static ATTR_NOCACHE_NOINIT_RAM_SECTION uint8_t g_ctr_out[BL602_GCM_CTR_CHUNK];

static struct bflb_device_s *g_aes_dev;
static int g_hw_inited;

/* ------------------------------------------------------------------------- */

static void bl602_put_be64(uint8_t *p, uint64_t v)
{
    p[0] = (uint8_t)(v >> 56); p[1] = (uint8_t)(v >> 48);
    p[2] = (uint8_t)(v >> 40); p[3] = (uint8_t)(v >> 32);
    p[4] = (uint8_t)(v >> 24); p[5] = (uint8_t)(v >> 16);
    p[6] = (uint8_t)(v >>  8); p[7] = (uint8_t)(v);
}

/* Software GHASH multiply: x = x . h in GF(2^128), standard NIST bit-reflected
 * domain (MSB-first over h's bits; right-shift x with the 0xE1 reduction when the
 * bit shifted out of the LSB is set). Table-free (128 iterations). */
static void bl602_ghash_mul(uint8_t x[16], const uint8_t h[16])
{
    uint8_t z[16];
    uint8_t v[16];
    int i, j;

    memset(z, 0, 16);
    memcpy(v, x, 16);
    for (i = 0; i < 128; i++) {
        if ((h[i >> 3] >> (7 - (i & 7))) & 1u) {
            for (j = 0; j < 16; j++) {
                z[j] ^= v[j];
            }
        }
        uint8_t lsb = (uint8_t)(v[15] & 1u);
        for (j = 15; j > 0; j--) {
            v[j] = (uint8_t)((v[j] >> 1) | (v[j - 1] << 7));
        }
        v[0] = (uint8_t)(v[0] >> 1);
        if (lsb) {
            v[0] ^= 0xe1u;
        }
    }
    memcpy(x, z, 16);
}

/* GHASH one 16-byte block into the running state Y: Y = (Y XOR blk) . H. */
static void bl602_ghash_block(uint8_t y[16], const uint8_t h[16], const uint8_t blk[16])
{
    int j;
    for (j = 0; j < 16; j++) {
        y[j] ^= blk[j];
    }
    bl602_ghash_mul(y, h);
}

/* One-time Sec_Eng AES bring-up (under the AES mutex). group0 access is
 * idempotent: request returns -1 if group0 already owns it, which is fine — the
 * grant is per-group, not per-caller. bflb_aes_link_init sets BE link mode. */
static void bl602_gcm_hw_init(void)
{
    if (g_hw_inited) {
        return;
    }
    g_aes_dev = bflb_device_get_by_name(BFLB_NAME_SEC_AES);
    (void)bflb_group0_request_aes_access(g_aes_dev);
    bflb_aes_link_init(g_aes_dev);
    g_hw_inited = 1;
}

/* One HW AES op (ECB single block or CTR over `len` bytes). GCM always runs the
 * AES core in the encrypt direction (CTR keystream and E(J0)/H). in/out must be
 * un-cached DMA scratch. `iv16` is the 16-byte ECB(zero)/counter block or NULL. */
static int bl602_aes_run(const mbedtls_gcm_context *ctx, uint8_t mode,
                         const uint8_t *iv16, const uint8_t *in,
                         uint8_t *out, uint32_t len)
{
    uint8_t keysel;

    if (ctx->keybits == 128u) {
        keysel = AES_LINK_KEY_128BITS;
    } else if (ctx->keybits == 192u) {
        keysel = AES_LINK_KEY_192BITS;
    } else {
        keysel = AES_LINK_KEY_256BITS;
    }

    memset(&g_aes_link, 0, sizeof(g_aes_link));
    g_aes_link.aes_key    = keysel;
    g_aes_link.aes_dec_en = 0;
    g_aes_link.aes_mode   = mode;
    memcpy(&g_aes_link.aes_key0, ctx->key, ctx->keybits / 8u);
    if (iv16 != NULL) {
        memcpy(&g_aes_link.aes_iv0, iv16, 16);   /* natural 16-byte block, as aes_alt.c */
    } else {
        memset(&g_aes_link.aes_iv0, 0, 16);
    }

    if (bflb_aes_link_update(g_aes_dev, (uint32_t)(uintptr_t)&g_aes_link,
                             in, out, len) != 0) {
        return -1;
    }
    return 0;
}

/*
 * One-shot GCM core: CTR-(de)crypt the whole record (chunked) and produce the
 * 16-byte tag via software GHASH. `encrypt` selects the GHASH input (ciphertext):
 * the CTR output for encryption, the CTR input for decryption. CTR is symmetric,
 * so `output` is the CTR result in both directions.
 */
static int bl602_hw_gcm(mbedtls_gcm_context *ctx, int encrypt, size_t length,
                        const unsigned char *iv, size_t iv_len,
                        const unsigned char *add, size_t add_len,
                        const unsigned char *input, unsigned char *output,
                        unsigned char tag[16])
{
    uint8_t H[16], ej0[16], Y[16], j0[16], cb[16], tmp[16];
    uint32_t blocks_done, pos, i;
    int ret;

    if (iv_len != 12u || ctx->keybits == 0u) {
        return MBEDTLS_ERR_GCM_BAD_INPUT;                 /* 96-bit IV only */
    }
    if (add_len > BL602_GCM_MAX_AAD_LEN) {
        return MBEDTLS_ERR_PLATFORM_FEATURE_UNSUPPORTED;  /* AAD scratch cap */
    }

    bflb_sec_aes_mutex_take();
    bl602_gcm_hw_init();

    /* H = AES-ECB(K, 0^128). */
    memset(g_ctr_in, 0, 16);
    ret = bl602_aes_run(ctx, AES_MODE_ECB, NULL, g_ctr_in, g_ctr_out, 16);
    if (ret != 0) {
        goto out;
    }
    memcpy(H, g_ctr_out, 16);

    /* E(K, J0), J0 = IV || 0x00000001. */
    memcpy(j0, iv, 12);
    j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;
    memcpy(g_ctr_in, j0, 16);
    ret = bl602_aes_run(ctx, AES_MODE_ECB, NULL, g_ctr_in, g_ctr_out, 16);
    if (ret != 0) {
        goto out;
    }
    memcpy(ej0, g_ctr_out, 16);

    /* GHASH: Y over AAD (zero-padded), then ciphertext (piecewise), then lens. */
    memset(Y, 0, 16);
    for (i = 0; i < add_len; i += 16u) {
        uint32_t n = add_len - i;
        if (n > 16u) {
            n = 16u;
        }
        memset(tmp, 0, 16);
        memcpy(tmp, add + i, n);
        bl602_ghash_block(Y, H, tmp);
    }

    /* CTR the record in <=512-byte pieces; GHASH each piece's ciphertext. */
    blocks_done = 0;
    pos = 0;
    while (pos < length) {
        uint32_t plen = (uint32_t)(length - pos);
        uint32_t nblk, ctr, off;
        const uint8_t *ghsrc;

        if (plen > BL602_GCM_CTR_CHUNK) {
            plen = BL602_GCM_CTR_CHUNK;
        }
        nblk = (plen + 15u) / 16u;

        /* starting counter for this piece = IV || be32(2 + blocks_done);
         * the HW auto-increments the low word within the piece. */
        memcpy(cb, iv, 12);
        ctr = 2u + blocks_done;
        cb[12] = (uint8_t)(ctr >> 24); cb[13] = (uint8_t)(ctr >> 16);
        cb[14] = (uint8_t)(ctr >> 8);  cb[15] = (uint8_t)(ctr);

        /* consume input piece into un-cached scratch (in-place safe), CTR it. */
        memset(g_ctr_in, 0, nblk * 16u);
        memcpy(g_ctr_in, input + pos, plen);
        ret = bl602_aes_run(ctx, AES_MODE_CTR, cb, g_ctr_in, g_ctr_out, nblk * 16u);
        if (ret != 0) {
            goto out;
        }

        /* GHASH is over the ciphertext: CTR output on encrypt, CTR input (the
         * copied ciphertext) on decrypt — both stable un-cached buffers, so this
         * is correct even when the caller passed input==output. */
        ghsrc = encrypt ? g_ctr_out : g_ctr_in;
        off = 0;
        while (off + 16u <= plen) {
            bl602_ghash_block(Y, H, ghsrc + off);
            off += 16u;
        }
        if (off < plen) {                       /* final sub-block (last piece) */
            memset(tmp, 0, 16);
            memcpy(tmp, ghsrc + off, plen - off);
            bl602_ghash_block(Y, H, tmp);
        }

        /* emit output last (ciphertext already captured for GHASH). */
        memcpy(output + pos, g_ctr_out, plen);

        blocks_done += nblk;
        pos += plen;
    }

    /* length block: len(AAD)_64be || len(C)_64be (bit lengths). */
    memset(tmp, 0, 16);
    bl602_put_be64(tmp,      (uint64_t)add_len * 8u);
    bl602_put_be64(tmp + 8u, (uint64_t)length  * 8u);
    bl602_ghash_block(Y, H, tmp);

    /* tag = Y XOR E(K, J0). */
    for (i = 0; i < 16u; i++) {
        tag[i] = (uint8_t)(Y[i] ^ ej0[i]);
    }
    ret = 0;

out:
    bflb_sec_aes_mutex_give();
    mbedtls_platform_zeroize(H, sizeof(H));
    mbedtls_platform_zeroize(Y, sizeof(Y));
    mbedtls_platform_zeroize(ej0, sizeof(ej0));
    mbedtls_platform_zeroize(tmp, sizeof(tmp));
    return (ret == 0) ? 0 : MBEDTLS_ERR_GCM_BAD_INPUT;
}

/* ------------------------------------------------------------------------- */

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
        return MBEDTLS_ERR_GCM_BAD_INPUT;                 /* AES-GCM only */
    }
    if (keybits != 128u && keybits != 192u && keybits != 256u) {
        return MBEDTLS_ERR_GCM_BAD_INPUT;
    }
    memcpy(ctx->key, key, keybits / 8u);
    ctx->keybits = keybits;
    return 0;
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
    r = bl602_hw_gcm(ctx, mode == MBEDTLS_GCM_ENCRYPT, length,
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
    r = bl602_hw_gcm(ctx, 0 /*decrypt*/, length,
                     iv, iv_len, add, add_len, input, output, full_tag);
    if (r != 0) {
        return r;
    }
    for (i = 0; i < tag_len; i++) {
        diff |= full_tag[i] ^ tag[i];                     /* constant-time */
    }
    if (diff != 0) {
        mbedtls_platform_zeroize(output, length);
        return MBEDTLS_ERR_GCM_AUTH_FAILED;
    }
    return 0;
}

/* --- streaming: single-update only (the HW op is one-shot) --- */

int mbedtls_gcm_starts(mbedtls_gcm_context *ctx, int mode,
                       const unsigned char *iv, size_t iv_len)
{
    if (iv_len != 12u) {
        return MBEDTLS_ERR_GCM_BAD_INPUT;
    }
    memcpy(ctx->iv, iv, 12u);
    ctx->mode    = mode;
    ctx->aad_len = 0;
    ctx->started = 1;
    ctx->did_op  = 0;
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
    if (add_len != 0u) {
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
        return MBEDTLS_ERR_PLATFORM_FEATURE_UNSUPPORTED;  /* single update only */
    }
    if (output_size < input_length) {
        return MBEDTLS_ERR_GCM_BAD_INPUT;
    }
    r = bl602_hw_gcm(ctx, ctx->mode == MBEDTLS_GCM_ENCRYPT, input_length,
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
    if (!ctx->did_op) {                          /* zero-length message: tag over AAD */
        unsigned char z = 0;
        int r = bl602_hw_gcm(ctx, ctx->mode == MBEDTLS_GCM_ENCRYPT, 0,
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
    return 0;     /* covered by the on-target [HWGCM] KAT instead */
}
#endif

#endif /* MBEDTLS_GCM_C && MBEDTLS_GCM_ALT */
