/*
 * rom_mbedtls_crypto.h — single self-contained header for the AmebaD A-cut
 * (RTL8721D / RTL8720DN) mask-ROM mbedTLS 2.4.0 crypto, callable as PURE SOFTWARE.
 *
 * Declares ONLY the symbols actually resident in the KM4 ROM (verified against
 * app_km4/rlx8721d_rom_symbol_acut.ld) and defines their context structs with the
 * EXACT ROM ABI. No mbedtls config, no include tree — replaces the vendored
 * mbedtls-2.4.0/include/ copy (which also collided with the upstream v3.6.4 headers
 * on the build path).
 *
 * ABI: structs reproduced verbatim from the SDK mbedtls-2.4.0 headers as the ROM
 * was built (config.h defines RTL_HW_CRYPTO, so mbedtls_aes_context carries the
 * extra enc_key/dec_key => 344 bytes). The ROM funcs memset()/operate on these, so
 * a wrong size is silent memory corruption — the _Static_asserts lock the sizes.
 *
 * Software only: call rom_mbedtls_init() (rom_mbedtls.h) ONCE first to force
 * use_hw_crypto_func=0; the IPSEC HW engine is never touched.
 *
 * Scope = symmetric ciphers + hashes + MD/HMAC layer. The ROM ALSO contains
 * (not declared here — heavy interdependent structs): bignum/MPI, RSA, ECP, ECDH,
 * ECDSA, ECJPAKE, DHM, PK, ASN.1, OID, CTR_DRBG, HMAC_DRBG. Add specific
 * declarations from mbedtls-2.4.0 if you need them; the symbols are linkable.
 *
 * Caveat: the ROM mbedtls_md_info_from_type()/md_info_from_string() return NULL on
 * this part, so the MD/HMAC abstraction layer is effectively unusable from ROM —
 * call the raw mbedtls_sha256/sha1/md5 directly (build HMAC by hand if needed).
 */
#ifndef ROM_MBEDTLS_CRYPTO_H
#define ROM_MBEDTLS_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================
 * AES — ABI: RTL_HW_CRYPTO layout (enc_key/dec_key present) => 344 bytes.
 * ====================================================================== */
#define MBEDTLS_AES_ENCRYPT     1
#define MBEDTLS_AES_DECRYPT     0

typedef struct
{
    int nr;                      /* number of rounds          */
    uint32_t *rk;                /* AES round keys            */
    uint32_t buf[68];            /* round-key buffer          */
    unsigned char enc_key[32];   /* RTL_HW_CRYPTO (unused on SW path) */
    unsigned char dec_key[32];   /* RTL_HW_CRYPTO (unused on SW path) */
} mbedtls_aes_context;
_Static_assert(sizeof(mbedtls_aes_context) == 344,
               "mbedtls_aes_context must be 344 bytes to match the ROM (RTL_HW_CRYPTO)");

void mbedtls_aes_init(mbedtls_aes_context *ctx);
void mbedtls_aes_free(mbedtls_aes_context *ctx);
int  mbedtls_aes_setkey_enc(mbedtls_aes_context *ctx, const unsigned char *key, unsigned int keybits);
int  mbedtls_aes_setkey_dec(mbedtls_aes_context *ctx, const unsigned char *key, unsigned int keybits);
int  mbedtls_aes_crypt_ecb(mbedtls_aes_context *ctx, int mode,
                           const unsigned char input[16], unsigned char output[16]);
int  mbedtls_aes_crypt_cbc(mbedtls_aes_context *ctx, int mode, size_t length,
                           unsigned char iv[16], const unsigned char *input, unsigned char *output);
int  mbedtls_aes_crypt_cfb128(mbedtls_aes_context *ctx, int mode, size_t length, size_t *iv_off,
                              unsigned char iv[16], const unsigned char *input, unsigned char *output);
int  mbedtls_aes_crypt_cfb8(mbedtls_aes_context *ctx, int mode, size_t length,
                            unsigned char iv[16], const unsigned char *input, unsigned char *output);
int  mbedtls_aes_crypt_ctr(mbedtls_aes_context *ctx, size_t length, size_t *nc_off,
                           unsigned char nonce_counter[16], unsigned char stream_block[16],
                           const unsigned char *input, unsigned char *output);
void mbedtls_aes_encrypt(mbedtls_aes_context *ctx, const unsigned char input[16], unsigned char output[16]);
void mbedtls_aes_decrypt(mbedtls_aes_context *ctx, const unsigned char input[16], unsigned char output[16]);

/* ======================================================================
 * ARC4 (RC4)
 * ====================================================================== */
typedef struct
{
    int x;
    int y;
    unsigned char m[256];
} mbedtls_arc4_context;
_Static_assert(sizeof(mbedtls_arc4_context) == 264, "mbedtls_arc4_context size mismatch");

void mbedtls_arc4_init(mbedtls_arc4_context *ctx);
void mbedtls_arc4_free(mbedtls_arc4_context *ctx);
void mbedtls_arc4_setup(mbedtls_arc4_context *ctx, const unsigned char *key, unsigned int keylen);
int  mbedtls_arc4_crypt(mbedtls_arc4_context *ctx, size_t length,
                        const unsigned char *input, unsigned char *output);

/* ======================================================================
 * MD5
 * ====================================================================== */
typedef struct
{
    uint32_t total[2];
    uint32_t state[4];
    unsigned char buffer[64];
} mbedtls_md5_context;
_Static_assert(sizeof(mbedtls_md5_context) == 88, "mbedtls_md5_context size mismatch");

void mbedtls_md5_init(mbedtls_md5_context *ctx);
void mbedtls_md5_free(mbedtls_md5_context *ctx);
void mbedtls_md5_clone(mbedtls_md5_context *dst, const mbedtls_md5_context *src);
void mbedtls_md5_starts(mbedtls_md5_context *ctx);
void mbedtls_md5_update(mbedtls_md5_context *ctx, const unsigned char *input, size_t ilen);
void mbedtls_md5_finish(mbedtls_md5_context *ctx, unsigned char output[16]);
void mbedtls_md5_process(mbedtls_md5_context *ctx, const unsigned char data[64]);
void mbedtls_md5(const unsigned char *input, size_t ilen, unsigned char output[16]);

/* ======================================================================
 * SHA-1
 * ====================================================================== */
typedef struct
{
    uint32_t total[2];
    uint32_t state[5];
    unsigned char buffer[64];
} mbedtls_sha1_context;
_Static_assert(sizeof(mbedtls_sha1_context) == 92, "mbedtls_sha1_context size mismatch");

void mbedtls_sha1_init(mbedtls_sha1_context *ctx);
void mbedtls_sha1_free(mbedtls_sha1_context *ctx);
void mbedtls_sha1_clone(mbedtls_sha1_context *dst, const mbedtls_sha1_context *src);
void mbedtls_sha1_starts(mbedtls_sha1_context *ctx);
void mbedtls_sha1_update(mbedtls_sha1_context *ctx, const unsigned char *input, size_t ilen);
void mbedtls_sha1_finish(mbedtls_sha1_context *ctx, unsigned char output[20]);
void mbedtls_sha1_process(mbedtls_sha1_context *ctx, const unsigned char data[64]);
void mbedtls_sha1(const unsigned char *input, size_t ilen, unsigned char output[20]);

/* ======================================================================
 * SHA-224 / SHA-256 (is224 selects)
 * ====================================================================== */
typedef struct
{
    uint32_t total[2];
    uint32_t state[8];
    unsigned char buffer[64];
    int is224;
} mbedtls_sha256_context;
_Static_assert(sizeof(mbedtls_sha256_context) == 108, "mbedtls_sha256_context size mismatch");

void mbedtls_sha256_init(mbedtls_sha256_context *ctx);
void mbedtls_sha256_free(mbedtls_sha256_context *ctx);
void mbedtls_sha256_clone(mbedtls_sha256_context *dst, const mbedtls_sha256_context *src);
void mbedtls_sha256_starts(mbedtls_sha256_context *ctx, int is224);
void mbedtls_sha256_update(mbedtls_sha256_context *ctx, const unsigned char *input, size_t ilen);
void mbedtls_sha256_finish(mbedtls_sha256_context *ctx, unsigned char output[32]);
void mbedtls_sha256_process(mbedtls_sha256_context *ctx, const unsigned char data[64]);
void mbedtls_sha256(const unsigned char *input, size_t ilen, unsigned char output[32], int is224);

/* ======================================================================
 * SHA-384 / SHA-512 (is384 selects)
 * ====================================================================== */
typedef struct
{
    uint64_t total[2];
    uint64_t state[8];
    unsigned char buffer[128];
    int is384;
} mbedtls_sha512_context;
/* 212 bytes of members, padded to 216 (8-byte alignment from the uint64_t). */
_Static_assert(sizeof(mbedtls_sha512_context) == 216, "mbedtls_sha512_context size mismatch");

void mbedtls_sha512_init(mbedtls_sha512_context *ctx);
void mbedtls_sha512_free(mbedtls_sha512_context *ctx);
void mbedtls_sha512_clone(mbedtls_sha512_context *dst, const mbedtls_sha512_context *src);
void mbedtls_sha512_starts(mbedtls_sha512_context *ctx, int is384);
void mbedtls_sha512_update(mbedtls_sha512_context *ctx, const unsigned char *input, size_t ilen);
void mbedtls_sha512_finish(mbedtls_sha512_context *ctx, unsigned char output[64]);
void mbedtls_sha512_process(mbedtls_sha512_context *ctx, const unsigned char data[128]);
void mbedtls_sha512(const unsigned char *input, size_t ilen, unsigned char output[64], int is384);

/* ======================================================================
 * MD / HMAC abstraction layer.
 * NOTE: mbedtls_md_info_from_type/_from_string return NULL on this ROM, so this
 * layer is effectively unusable here — prefer the raw hash functions above.
 * ====================================================================== */
typedef enum {
    MBEDTLS_MD_NONE = 0,
    MBEDTLS_MD_MD2,
    MBEDTLS_MD_MD4,
    MBEDTLS_MD_MD5,
    MBEDTLS_MD_SHA1,
    MBEDTLS_MD_SHA224,
    MBEDTLS_MD_SHA256,
    MBEDTLS_MD_SHA384,
    MBEDTLS_MD_SHA512,
    MBEDTLS_MD_RIPEMD160,
} mbedtls_md_type_t;

typedef struct mbedtls_md_info_t mbedtls_md_info_t;   /* opaque */

typedef struct {
    const mbedtls_md_info_t *md_info;
    void *md_ctx;
    void *hmac_ctx;
} mbedtls_md_context_t;

/* The per-algorithm info descriptors (ROM rodata). */
extern const mbedtls_md_info_t mbedtls_md5_info;
extern const mbedtls_md_info_t mbedtls_sha1_info;
extern const mbedtls_md_info_t mbedtls_sha224_info;
extern const mbedtls_md_info_t mbedtls_sha256_info;
extern const mbedtls_md_info_t mbedtls_sha384_info;
extern const mbedtls_md_info_t mbedtls_sha512_info;

const int               *mbedtls_md_list(void);
const mbedtls_md_info_t *mbedtls_md_info_from_string(const char *md_name);
const mbedtls_md_info_t *mbedtls_md_info_from_type(mbedtls_md_type_t md_type);
void mbedtls_md_init(mbedtls_md_context_t *ctx);
void mbedtls_md_free(mbedtls_md_context_t *ctx);
int  mbedtls_md_init_ctx(mbedtls_md_context_t *ctx, const mbedtls_md_info_t *md_info);
int  mbedtls_md_setup(mbedtls_md_context_t *ctx, const mbedtls_md_info_t *md_info, int hmac);
int  mbedtls_md_clone(mbedtls_md_context_t *dst, const mbedtls_md_context_t *src);
unsigned char     mbedtls_md_get_size(const mbedtls_md_info_t *md_info);
mbedtls_md_type_t mbedtls_md_get_type(const mbedtls_md_info_t *md_info);
const char       *mbedtls_md_get_name(const mbedtls_md_info_t *md_info);
int  mbedtls_md_starts(mbedtls_md_context_t *ctx);
int  mbedtls_md_update(mbedtls_md_context_t *ctx, const unsigned char *input, size_t ilen);
int  mbedtls_md_finish(mbedtls_md_context_t *ctx, unsigned char *output);
int  mbedtls_md(const mbedtls_md_info_t *md_info, const unsigned char *input, size_t ilen, unsigned char *output);
int  mbedtls_md_process(mbedtls_md_context_t *ctx, const unsigned char *data);
int  mbedtls_md_hmac_starts(mbedtls_md_context_t *ctx, const unsigned char *key, size_t keylen);
int  mbedtls_md_hmac_update(mbedtls_md_context_t *ctx, const unsigned char *input, size_t ilen);
int  mbedtls_md_hmac_finish(mbedtls_md_context_t *ctx, unsigned char *output);
int  mbedtls_md_hmac_reset(mbedtls_md_context_t *ctx);
int  mbedtls_md_hmac(const mbedtls_md_info_t *md_info, const unsigned char *key, size_t keylen,
                     const unsigned char *input, size_t ilen, unsigned char *output);

/* ======================================================================
 * Base64
 * ====================================================================== */
int mbedtls_base64_encode(unsigned char *dst, size_t dlen, size_t *olen,
                          const unsigned char *src, size_t slen);
int mbedtls_base64_decode(unsigned char *dst, size_t dlen, size_t *olen,
                          const unsigned char *src, size_t slen);

#ifdef __cplusplus
}
#endif

#endif /* ROM_MBEDTLS_CRYPTO_H */
