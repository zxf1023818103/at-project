/* Hostap's crypto ABI backed by the project's mbedTLS 3.x implementation.
 * Only group 19 is exposed: the NAN pairing profile uses P-256 and CCMP-128.
 * No private scalar or authentication key is logged by this adapter. */
#include "utils/includes.h"
#include "utils/common.h"
#include "utils/wpabuf.h"
#include "crypto/crypto.h"
#include "crypto/aes.h"
#include "crypto/aes_wrap.h"
#include "mbedtls/bignum.h"
#include "mbedtls/ecp.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/md.h"
#include "mbedtls/aes.h"
#include "mbedtls/cmac.h"

extern int green2_nan_random(void *out, size_t len);
static int rng(void *ctx, unsigned char *out, size_t len)
{ (void)ctx; return green2_nan_random(out, len); }
static int point_multiply(mbedtls_ecp_group *group, mbedtls_ecp_point *result,
                          const mbedtls_mpi *scalar, const mbedtls_ecp_point *point)
{
#ifdef AT_GREEN2_NAN
    extern int green2_nan_p256_mul(mbedtls_ecp_group *, mbedtls_ecp_point *,
                                  const mbedtls_mpi *, const mbedtls_ecp_point *);
    int rc = green2_nan_p256_mul(group, result, scalar, point);
    if (rc != MBEDTLS_ERR_ECP_FEATURE_UNAVAILABLE) return rc;
#endif
    return mbedtls_ecp_mul(group, result, scalar, point, rng, NULL);
}
int crypto_get_random(void *out, size_t len) { return green2_nan_random(out, len); }

static int digest(mbedtls_md_type_t type, const u8 *key, size_t keylen,
                  size_t count, const u8 *data[], const size_t len[], u8 *out)
{
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(type);
    int rc = -1;
    if (!info) return -1;
    mbedtls_md_init(&ctx);
    if (mbedtls_md_setup(&ctx, info, key != NULL)) goto done;
    rc = key ? mbedtls_md_hmac_starts(&ctx, key, keylen) : mbedtls_md_starts(&ctx);
    for (size_t i = 0; !rc && i < count; ++i)
        rc = key ? mbedtls_md_hmac_update(&ctx, data[i], len[i]) : mbedtls_md_update(&ctx, data[i], len[i]);
    if (!rc) rc = key ? mbedtls_md_hmac_finish(&ctx, out) : mbedtls_md_finish(&ctx, out);
done:
    mbedtls_md_free(&ctx);
    return rc;
}
#define HASH(name, type) \
int name##_vector(size_t n, const u8 *a[], const size_t l[], u8 *out) \
{ return digest(type, NULL, 0, n, a, l, out); } \
int hmac_##name##_vector(const u8 *k, size_t kl, size_t n, const u8 *a[], const size_t l[], u8 *out) \
{ return digest(type, k, kl, n, a, l, out); } \
int hmac_##name(const u8 *k, size_t kl, const u8 *a, size_t l, u8 *out) \
{ return hmac_##name##_vector(k, kl, 1, &a, &l, out); }
HASH(sha1, MBEDTLS_MD_SHA1)
HASH(sha256, MBEDTLS_MD_SHA256)
HASH(sha384, MBEDTLS_MD_SHA384)
HASH(sha512, MBEDTLS_MD_SHA512)

void *aes_encrypt_init(const u8 *key, size_t len)
{
    mbedtls_aes_context *c = os_zalloc(sizeof(*c));
    if (!c) return NULL;
    mbedtls_aes_init(c);
    if (mbedtls_aes_setkey_enc(c, key, len * 8)) { mbedtls_aes_free(c); os_free(c); return NULL; }
    return c;
}
int aes_encrypt(void *c, const u8 *in, u8 *out)
{ return mbedtls_aes_crypt_ecb(c, MBEDTLS_AES_ENCRYPT, in, out); }
void aes_encrypt_deinit(void *c) { if (c) { mbedtls_aes_free(c); os_free(c); } }
void *aes_decrypt_init(const u8 *key, size_t len)
{
    mbedtls_aes_context *c = os_zalloc(sizeof(*c));
    if (!c) return NULL;
    mbedtls_aes_init(c);
    if (mbedtls_aes_setkey_dec(c, key, len * 8)) { mbedtls_aes_free(c); os_free(c); return NULL; }
    return c;
}
int aes_decrypt(void *c, const u8 *in, u8 *out)
{ return mbedtls_aes_crypt_ecb(c, MBEDTLS_AES_DECRYPT, in, out); }
void aes_decrypt_deinit(void *c) { aes_encrypt_deinit(c); }
int omac1_aes_vector(const u8 *key, size_t keylen, size_t n, const u8 *a[], const size_t l[], u8 *mac)
{
    mbedtls_cipher_context_t ctx;
    mbedtls_cipher_type_t type = keylen == 16 ? MBEDTLS_CIPHER_AES_128_ECB :
        keylen == 24 ? MBEDTLS_CIPHER_AES_192_ECB : MBEDTLS_CIPHER_AES_256_ECB;
    const mbedtls_cipher_info_t *info = mbedtls_cipher_info_from_type(type);
    int rc = -1;
    if (!info || (keylen != 16 && keylen != 24 && keylen != 32)) return -1;
    mbedtls_cipher_init(&ctx);
    if (mbedtls_cipher_setup(&ctx, info)) goto done;
    rc = mbedtls_cipher_cmac_starts(&ctx, key, keylen * 8);
    for (size_t i = 0; !rc && i < n; ++i) rc = mbedtls_cipher_cmac_update(&ctx, a[i], l[i]);
    if (!rc) rc = mbedtls_cipher_cmac_finish(&ctx, mac);
done:
    mbedtls_cipher_free(&ctx);
    return rc;
}
int omac1_aes_128_vector(const u8 *k, size_t n, const u8 *a[], const size_t l[], u8 *out)
{ return omac1_aes_vector(k, 16, n, a, l, out); }
int omac1_aes_128(const u8 *k, const u8 *a, size_t l, u8 *out)
{ return omac1_aes_vector(k, 16, 1, &a, &l, out); }

#define BN(x) ((mbedtls_mpi *)(x))
#define CBN(x) ((const mbedtls_mpi *)(x))
struct crypto_bignum *crypto_bignum_init(void)
{
    mbedtls_mpi *b = os_zalloc(sizeof(*b));
    if (b) mbedtls_mpi_init(b);
    return (void *)b;
}
void crypto_bignum_deinit(struct crypto_bignum *b, int clear)
{ (void)clear; if (b) { mbedtls_mpi_free(BN(b)); os_free(b); } }
struct crypto_bignum *crypto_bignum_init_set(const u8 *data, size_t len)
{
    struct crypto_bignum *b = crypto_bignum_init();
    if (b && mbedtls_mpi_read_binary(BN(b), data, len)) { crypto_bignum_deinit(b, 1); b = NULL; }
    return b;
}
struct crypto_bignum *crypto_bignum_init_uint(unsigned int v)
{
    u8 data[4]; WPA_PUT_BE32(data, v);
    return crypto_bignum_init_set(data, sizeof(data));
}
int crypto_bignum_to_bin(const struct crypto_bignum *b, u8 *buf, size_t max, size_t pad)
{
    size_t n = mbedtls_mpi_size(CBN(b));
    if (n < pad) n = pad;
    if (n > max || mbedtls_mpi_write_binary(CBN(b), buf, n)) return -1;
    return n;
}
int crypto_bignum_rand(struct crypto_bignum *r, const struct crypto_bignum *m)
{ return mbedtls_mpi_random(BN(r), 0, CBN(m), rng, NULL); }
int crypto_bignum_add(const struct crypto_bignum *a, const struct crypto_bignum *b, struct crypto_bignum *c)
{ return mbedtls_mpi_add_mpi(BN(c), CBN(a), CBN(b)); }
int crypto_bignum_sub(const struct crypto_bignum *a, const struct crypto_bignum *b, struct crypto_bignum *c)
{ return mbedtls_mpi_sub_mpi(BN(c), CBN(a), CBN(b)); }
int crypto_bignum_mod(const struct crypto_bignum *a, const struct crypto_bignum *b, struct crypto_bignum *c)
{ return mbedtls_mpi_mod_mpi(BN(c), CBN(a), CBN(b)); }
int crypto_bignum_div(const struct crypto_bignum *a, const struct crypto_bignum *b, struct crypto_bignum *c)
{ return mbedtls_mpi_div_mpi(BN(c), NULL, CBN(a), CBN(b)); }
int crypto_bignum_exptmod(const struct crypto_bignum *a, const struct crypto_bignum *b, const struct crypto_bignum *c, struct crypto_bignum *d)
{
    /* Hostap permits an in-place exponent/result (SAE SSWU uses it).
     * mbedTLS explicitly forbids the result aliasing the exponent or modulus. */
    mbedtls_mpi result; mbedtls_mpi_init(&result);
    int rc = mbedtls_mpi_exp_mod(&result, CBN(a), CBN(b), CBN(c), NULL);
    if (!rc) mbedtls_mpi_swap(BN(d), &result);
    mbedtls_mpi_free(&result); return rc;
}
int crypto_bignum_inverse(const struct crypto_bignum *a, const struct crypto_bignum *b, struct crypto_bignum *c)
{ return mbedtls_mpi_inv_mod(BN(c), CBN(a), CBN(b)); }
int crypto_bignum_mulmod(const struct crypto_bignum *a, const struct crypto_bignum *b, const struct crypto_bignum *m, struct crypto_bignum *r)
{
    mbedtls_mpi tmp; mbedtls_mpi_init(&tmp);
    int rc = mbedtls_mpi_mul_mpi(&tmp, CBN(a), CBN(b));
    if (!rc) rc = mbedtls_mpi_mod_mpi(BN(r), &tmp, CBN(m));
    mbedtls_mpi_free(&tmp); return rc;
}
int crypto_bignum_addmod(const struct crypto_bignum *a, const struct crypto_bignum *b, const struct crypto_bignum *m, struct crypto_bignum *r)
{
    mbedtls_mpi tmp; mbedtls_mpi_init(&tmp);
    int rc = mbedtls_mpi_add_mpi(&tmp, CBN(a), CBN(b));
    if (!rc) rc = mbedtls_mpi_mod_mpi(BN(r), &tmp, CBN(m));
    mbedtls_mpi_free(&tmp); return rc;
}
int crypto_bignum_sqrmod(const struct crypto_bignum *a, const struct crypto_bignum *m, struct crypto_bignum *r)
{ return crypto_bignum_mulmod(a, a, m, r); }
int crypto_bignum_rshift(const struct crypto_bignum *a, int n, struct crypto_bignum *r)
{ int rc = mbedtls_mpi_copy(BN(r), CBN(a)); return rc ? rc : mbedtls_mpi_shift_r(BN(r), n); }
int crypto_bignum_cmp(const struct crypto_bignum *a, const struct crypto_bignum *b)
{ return mbedtls_mpi_cmp_mpi(CBN(a), CBN(b)); }
int crypto_bignum_is_zero(const struct crypto_bignum *a) { return mbedtls_mpi_cmp_int(CBN(a), 0) == 0; }
int crypto_bignum_is_one(const struct crypto_bignum *a) { return mbedtls_mpi_cmp_int(CBN(a), 1) == 0; }
int crypto_bignum_is_odd(const struct crypto_bignum *a) { return mbedtls_mpi_get_bit(CBN(a), 0); }
int crypto_bignum_legendre(const struct crypto_bignum *a, const struct crypto_bignum *p)
{
    mbedtls_mpi exponent, result;
    mbedtls_mpi_init(&exponent); mbedtls_mpi_init(&result);
    int rc = -2;
    if (mbedtls_mpi_sub_int(&exponent, CBN(p), 1) || mbedtls_mpi_shift_r(&exponent, 1) ||
        mbedtls_mpi_exp_mod(&result, CBN(a), &exponent, CBN(p), NULL)) goto done;
    rc = mbedtls_mpi_cmp_int(&result, 0) == 0 ? 0 : mbedtls_mpi_cmp_int(&result, 1) == 0 ? 1 : -1;
done:
    mbedtls_mpi_free(&result); mbedtls_mpi_free(&exponent); return rc;
}

struct crypto_ec { mbedtls_ecp_group group; mbedtls_mpi a; };
#define PT(x) ((mbedtls_ecp_point *)(x))
#define CPT(x) ((const mbedtls_ecp_point *)(x))
struct crypto_ec *crypto_ec_init(int group)
{
    struct crypto_ec *e;
    if (group != 19 || !(e = os_zalloc(sizeof(*e)))) return NULL;
    mbedtls_ecp_group_init(&e->group); mbedtls_mpi_init(&e->a);
    if (mbedtls_ecp_group_load(&e->group, MBEDTLS_ECP_DP_SECP256R1) ||
        mbedtls_mpi_sub_int(&e->a, &e->group.P, 3)) { crypto_ec_deinit(e); return NULL; }
    return e;
}
void crypto_ec_deinit(struct crypto_ec *e)
{ if (e) { mbedtls_ecp_group_free(&e->group); mbedtls_mpi_free(&e->a); os_free(e); } }
size_t crypto_ec_prime_len(struct crypto_ec *e) { return mbedtls_mpi_size(&e->group.P); }
size_t crypto_ec_prime_len_bits(struct crypto_ec *e) { return mbedtls_mpi_bitlen(&e->group.P); }
size_t crypto_ec_order_len(struct crypto_ec *e) { return mbedtls_mpi_size(&e->group.N); }
const struct crypto_bignum *crypto_ec_get_prime(struct crypto_ec *e) { return (void *)&e->group.P; }
const struct crypto_bignum *crypto_ec_get_order(struct crypto_ec *e) { return (void *)&e->group.N; }
const struct crypto_bignum *crypto_ec_get_a(struct crypto_ec *e) { return (void *)&e->a; }
const struct crypto_bignum *crypto_ec_get_b(struct crypto_ec *e) { return (void *)&e->group.B; }
const struct crypto_ec_point *crypto_ec_get_generator(struct crypto_ec *e) { return (void *)&e->group.G; }
struct crypto_ec_point *crypto_ec_point_init(struct crypto_ec *e)
{ (void)e; mbedtls_ecp_point *p = os_zalloc(sizeof(*p)); if (p) mbedtls_ecp_point_init(p); return (void *)p; }
void crypto_ec_point_deinit(struct crypto_ec_point *p, int clear)
{ (void)clear; if (p) { mbedtls_ecp_point_free(PT(p)); os_free(p); } }
int crypto_ec_point_x(struct crypto_ec *e, const struct crypto_ec_point *p, struct crypto_bignum *x)
{ (void)e; return mbedtls_mpi_copy(BN(x), &CPT(p)->MBEDTLS_PRIVATE(X)); }
int crypto_ec_point_to_bin(struct crypto_ec *e, const struct crypto_ec_point *p, u8 *x, u8 *y)
{
    size_t n = crypto_ec_prime_len(e);
    if (x && mbedtls_mpi_write_binary(&CPT(p)->MBEDTLS_PRIVATE(X), x, n)) return -1;
    if (y && mbedtls_mpi_write_binary(&CPT(p)->MBEDTLS_PRIVATE(Y), y, n)) return -1;
    return 0;
}
struct crypto_ec_point *crypto_ec_point_from_bin(struct crypto_ec *e, const u8 *xy)
{
    struct crypto_ec_point *p = crypto_ec_point_init(e); size_t n = crypto_ec_prime_len(e);
    if (!p) return NULL;
    if (mbedtls_mpi_read_binary(&PT(p)->MBEDTLS_PRIVATE(X), xy, n) ||
        mbedtls_mpi_read_binary(&PT(p)->MBEDTLS_PRIVATE(Y), xy + n, n) ||
        mbedtls_mpi_lset(&PT(p)->MBEDTLS_PRIVATE(Z), 1) ||
        mbedtls_ecp_check_pubkey(&e->group, PT(p))) { crypto_ec_point_deinit(p, 1); return NULL; }
    return p;
}
int crypto_ec_point_add(struct crypto_ec *e, const struct crypto_ec_point *a, const struct crypto_ec_point *b, struct crypto_ec_point *r)
{
    mbedtls_mpi one; mbedtls_mpi_init(&one); int rc = mbedtls_mpi_lset(&one, 1);
    if (!rc) rc = mbedtls_ecp_muladd(&e->group, PT(r), &one, CPT(a), &one, CPT(b));
    mbedtls_mpi_free(&one); return rc;
}
int crypto_ec_point_mul(struct crypto_ec *e, const struct crypto_ec_point *p, const struct crypto_bignum *b, struct crypto_ec_point *r)
{ return point_multiply(&e->group, PT(r), CBN(b), CPT(p)); }
int crypto_ec_point_invert(struct crypto_ec *e, struct crypto_ec_point *p)
{
    mbedtls_mpi *y = &PT(p)->MBEDTLS_PRIVATE(Y);
    return mbedtls_mpi_cmp_int(y, 0) ? mbedtls_mpi_sub_mpi(y, &e->group.P, y) : 0;
}
struct crypto_bignum *crypto_ec_point_compute_y_sqr(struct crypto_ec *e, const struct crypto_bignum *x)
{
    struct crypto_bignum *r = crypto_bignum_init(); mbedtls_mpi tmp;
    mbedtls_mpi_init(&tmp);
    if (!r) return NULL;
    if (crypto_bignum_sqrmod(x, (void *)&e->group.P, r) ||
        crypto_bignum_mulmod(r, x, (void *)&e->group.P, r) ||
        mbedtls_mpi_mul_mpi(&tmp, &e->a, CBN(x)) || mbedtls_mpi_add_mpi(BN(r), BN(r), &tmp) ||
        mbedtls_mpi_add_mpi(BN(r), BN(r), &e->group.B) || mbedtls_mpi_mod_mpi(BN(r), BN(r), &e->group.P)) {
        crypto_bignum_deinit(r, 1); r = NULL;
    }
    mbedtls_mpi_free(&tmp); return r;
}
int crypto_ec_point_is_at_infinity(struct crypto_ec *e, const struct crypto_ec_point *p)
{ (void)e; return mbedtls_ecp_is_zero((mbedtls_ecp_point *)p); }
int crypto_ec_point_is_on_curve(struct crypto_ec *e, const struct crypto_ec_point *p)
{ return mbedtls_ecp_check_pubkey(&e->group, CPT(p)) == 0; }
int crypto_ec_point_cmp(const struct crypto_ec *e, const struct crypto_ec_point *a, const struct crypto_ec_point *b)
{ (void)e; return mbedtls_ecp_point_cmp(CPT(a), CPT(b)); }

struct crypto_ecdh { struct crypto_ec *ec; mbedtls_mpi secret; mbedtls_ecp_point pub; };
struct crypto_ecdh *crypto_ecdh_init(int group)
{
    struct crypto_ecdh *d = os_zalloc(sizeof(*d));
    if (!d) return NULL;
    mbedtls_mpi_init(&d->secret); mbedtls_ecp_point_init(&d->pub);
    d->ec = crypto_ec_init(group);
    if (!d->ec || mbedtls_ecp_gen_privkey(&d->ec->group, &d->secret, rng, NULL) ||
        point_multiply(&d->ec->group, &d->pub, &d->secret, &d->ec->group.G)) {
        crypto_ecdh_deinit(d); return NULL;
    }
    return d;
}
void crypto_ecdh_deinit(struct crypto_ecdh *d)
{ if (d) { crypto_ec_deinit(d->ec); mbedtls_mpi_free(&d->secret); mbedtls_ecp_point_free(&d->pub); os_free(d); } }
size_t crypto_ecdh_prime_len(struct crypto_ecdh *d) { return crypto_ec_prime_len(d->ec); }
struct wpabuf *crypto_ecdh_get_pubkey(struct crypto_ecdh *d, int inc_y)
{
    size_t n = crypto_ecdh_prime_len(d); struct wpabuf *out = wpabuf_alloc(n * (inc_y ? 2 : 1));
    if (!out) return NULL;
    u8 *xy = wpabuf_put(out, n * (inc_y ? 2 : 1));
    if (crypto_ec_point_to_bin(d->ec, (void *)&d->pub, xy, inc_y ? xy + n : NULL)) {
        wpabuf_free(out); return NULL;
    }
    return out;
}
struct wpabuf *crypto_ecdh_set_peerkey(struct crypto_ecdh *d, int inc_y, const u8 *key, size_t len)
{
    size_t n = crypto_ecdh_prime_len(d);
    struct wpabuf *out = NULL; struct crypto_bignum *y2 = NULL;
    mbedtls_ecp_point p; mbedtls_mpi exponent, z;
    mbedtls_ecp_point_init(&p); mbedtls_mpi_init(&exponent); mbedtls_mpi_init(&z);
    if (!key || len != n * (inc_y ? 2 : 1) ||
        mbedtls_mpi_read_binary(&p.MBEDTLS_PRIVATE(X), key, n) ||
        mbedtls_mpi_cmp_mpi(&p.MBEDTLS_PRIVATE(X), &d->ec->group.P) >= 0) goto done;
    if (inc_y) {
        if (mbedtls_mpi_read_binary(&p.MBEDTLS_PRIVATE(Y), key + n, n)) goto done;
    } else {
        /* P-256 has p = 3 mod 4. PASN's compressed key selects even y. */
        y2 = crypto_ec_point_compute_y_sqr(d->ec, (void *)&p.MBEDTLS_PRIVATE(X));
        if (!y2 || mbedtls_mpi_add_int(&exponent, &d->ec->group.P, 1) ||
            mbedtls_mpi_shift_r(&exponent, 2) ||
            mbedtls_mpi_exp_mod(&p.MBEDTLS_PRIVATE(Y), BN(y2), &exponent, &d->ec->group.P, NULL)) goto done;
        if (mbedtls_mpi_get_bit(&p.MBEDTLS_PRIVATE(Y), 0) &&
            mbedtls_mpi_sub_mpi(&p.MBEDTLS_PRIVATE(Y), &d->ec->group.P, &p.MBEDTLS_PRIVATE(Y))) goto done;
    }
    if (mbedtls_mpi_lset(&p.MBEDTLS_PRIVATE(Z), 1) || mbedtls_ecp_check_pubkey(&d->ec->group, &p) ||
        point_multiply(&d->ec->group, &p, &d->secret, &p) ||
        mbedtls_mpi_copy(&z, &p.MBEDTLS_PRIVATE(X))) goto done;
    out = wpabuf_alloc(n);
    if (out && mbedtls_mpi_write_binary(&z, wpabuf_put(out, n), n)) { wpabuf_clear_free(out); out = NULL; }
done:
    crypto_bignum_deinit(y2, 1); mbedtls_mpi_free(&z); mbedtls_mpi_free(&exponent); mbedtls_ecp_point_free(&p);
    return out;
}
