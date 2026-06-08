/*
 * wolfcrypt_sm_glue.c — back NimBLE 1.9.0's Security Manager crypto with wolfCrypt
 * (component/amebad/wolfssl, v5.9.1) instead of the bundled tinycrypt.
 *
 * NimBLE 1.9.0's ble_sm_alg.c hard-codes the tinycrypt API (no BLE_CRYPTO_STACK_*
 * switch), so rather than patch upstream we re-implement the small primitive surface
 * it (and BLE Mesh) calls, in terms of wolfCrypt:
 *
 *   AES-128 single block   tc_aes128_set_encrypt_key / tc_aes_encrypt
 *                          -> wc_AesSetKeyDirect / wc_AesEncryptDirect (ECB).
 *                          tinycrypt's cmac_mode.c is KEPT and rides on these, so
 *                          AES-CMAC (f4/f5/f6/g2) flows through wolfCrypt too.
 *   ECDH P-256             uECC_make_key / uECC_shared_secret / uECC_valid_public_key
 *                          / uECC_set_rng -> wc_ecc_*.
 *
 * The matching upstream sources are dropped from the build (component/nimble
 * CMakeLists REMOVE_ITEM): ext/tinycrypt/src/{aes_encrypt,ecc,ecc_dh,ecc_dsa,
 * ecc_platform_specific}.c. cmac_mode.c / utils.c / aes_decrypt.c stay.
 *
 * curve_secp256r1 is a `static const` defined in tinycrypt/ecc.h, so every TU that
 * includes it (ble_sm_alg.c, mesh) emits its own copy — whose initializer takes the
 * address of double_jacobian_default / x_side_default / vli_mmod_fast_secp256r1
 * (normally in ecc.c). We ignore the curve struct entirely (always P-256 via
 * wolfCrypt), so those are never called — but the linker needs them, hence the
 * empty stubs at the bottom.
 *
 * Byte order: micro-ecc (this tinycrypt build) works in BIG-ENDIAN byte arrays —
 * public = X(32)||Y(32), private = d(32), secret = X(32). ble_sm_alg swaps to/from
 * BLE little-endian around the calls; wolfCrypt's unsigned-bin import/export is
 * big-endian, so the formats line up 1:1.
 *
 * RNG: the ECDH PRIVATE KEY is drawn from NimBLE's RNG (uECC_set_rng -> the
 * controller HCI LE_Rand, crypto-strong). wolfCrypt's WC_RNG (CUSTOM_RAND_GENERATE_
 * BLOCK) is used ONLY for ECC point blinding (ECC_TIMING_RESISTANT), never for key
 * material.
 *
 * Public-key export uses wc_ecc_export_public_raw() — the natural wolfCrypt API.
 *
 * HISTORICAL BOOT HANG (root-caused + fixed 2026-06-08): linking this path used to
 * hang KM4 in app_start before main() (SecureFault AUVIOL). It was long mis-blamed
 * on a "Realtek XIP fetch" HW quirk; the real cause is a LINKER SECTION-ALIGNMENT
 * bug. wc_ecc_export_public_raw -> wc_export_int (wolfmath.c) pulls sp_tohex's TU
 * (sp_int.c), which carries a 0x40-aligned input section; that raises the output
 * .xip_image2.text alignment to 0x40. KM4_IMG2 ORIGIN is only 0x20-aligned
 * (0x0E000020), so the section starts at 0x0E000040 and __flash_text_start__ moves
 * +0x20 — but the 32-byte image2 header and the boot ROM's *linear* XIP MMU both map
 * the code right after the header (XIP 0x0E000020). The 0x20 gap displaces all code
 * -0x20, so every branch/entry lands +0x20 off -> wild jump -> AUVIOL.
 * FIX (app_km4/rlx8721d_img2_is.ld): pin the section VMA with
 *   .xip_image2.text ORIGIN(KM4_IMG2) :
 * so __flash_text_start__ stays 0x0E000020 regardless of input alignment. General
 * (guards any 0x40-aligned input), so wc_ecc_export_public_raw is safe to use.
 */
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/aes.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/random.h>
#include <string.h>

#include "tinycrypt/aes.h"        /* struct tc_aes_key_sched_struct, TCAesKeySched_t */
#include "tinycrypt/constants.h"  /* TC_CRYPTO_SUCCESS / TC_CRYPTO_FAIL */
#include "tinycrypt/ecc.h"        /* uECC_Curve, uECC_RNG_Function, uECC_word_t */
#include "tinycrypt/ecc_dh.h"     /* uECC_make_key / uECC_shared_secret prototypes */

extern void *pvPortMalloc(unsigned int);
extern void  vPortFree(void *);
extern unsigned int DiagPrintf(const char *fmt, ...);

/* 1 = full wolfCrypt SM (AES/CMAC + ECDH P-256), tinycrypt ecc*.c dropped (shipped).
 * 0 = AES/CMAC on wolfCrypt only, ECDH stays on tinycrypt (keep tinycrypt ecc*.c in
 *     the component/nimble CMakeLists build). */
#define SM_ECC_WOLF 1

/* ---------------------------------------------------------------- AES (ECB) -- */

int
tc_aes128_set_encrypt_key(TCAesKeySched_t s, const uint8_t *k)
{
    if (s == (TCAesKeySched_t)0 || k == (const uint8_t *)0) {
        return TC_CRYPTO_FAIL;
    }
    /* The 176-byte schedule storage easily holds the raw 16-byte key; expand it
     * per-block in tc_aes_encrypt (SM/CMAC process only a handful of blocks). */
    memcpy(s->words, k, 16);
    return TC_CRYPTO_SUCCESS;
}

int
tc_aes_encrypt(uint8_t *out, const uint8_t *in, const TCAesKeySched_t s)
{
    Aes aes;
    int rc;

    if (out == (uint8_t *)0 || in == (const uint8_t *)0 || s == (TCAesKeySched_t)0) {
        return TC_CRYPTO_FAIL;
    }
    if (wc_AesInit(&aes, (void *)0, INVALID_DEVID) != 0) {
        return TC_CRYPTO_FAIL;
    }
    rc = wc_AesSetKeyDirect(&aes, (const byte *)s->words, 16, (const byte *)0,
                            AES_ENCRYPTION);
    if (rc == 0) {
        rc = wc_AesEncryptDirect(&aes, out, in);
    }
    wc_AesFree(&aes);
    return (rc == 0) ? TC_CRYPTO_SUCCESS : TC_CRYPTO_FAIL;
}

/* ------------------------------------------------------------- ECDH (P-256) -- */
#if SM_ECC_WOLF

static WC_RNG s_blind_rng;        /* point-blinding only; NOT key material */
static int    s_blind_rng_ok;
static uECC_RNG_Function g_uecc_rng;

static int
ensure_blind_rng(void)
{
    if (!s_blind_rng_ok) {
        if (wc_InitRng(&s_blind_rng) != 0) {
            return -1;
        }
        s_blind_rng_ok = 1;
    }
    return 0;
}

void
uECC_set_rng(uECC_RNG_Function rng_function)
{
    g_uecc_rng = rng_function;
}

uECC_RNG_Function
uECC_get_rng(void)
{
    return g_uecc_rng;
}

/* pub: 64B (X||Y, big-endian), priv: 32B (big-endian). Returns TC_CRYPTO_SUCCESS. */
int
uECC_make_key(uint8_t *p_public_key, uint8_t *p_private_key, uECC_Curve curve)
{
    int tries;

    (void)curve;
    if (p_public_key == (uint8_t *)0 || p_private_key == (uint8_t *)0 ||
        g_uecc_rng == (uECC_RNG_Function)0) {
        return TC_CRYPTO_FAIL;
    }
    if (ensure_blind_rng() != 0) {
        return TC_CRYPTO_FAIL;
    }

    for (tries = 0; tries < 16; tries++) {
        ecc_key *key = (ecc_key *)pvPortMalloc(sizeof(ecc_key));
        int rc;

        if (key == (ecc_key *)0) {
            return TC_CRYPTO_FAIL;
        }
        if (g_uecc_rng(p_private_key, 32) != 1) {
            vPortFree(key);
            return TC_CRYPTO_FAIL;
        }
        if (wc_ecc_init(key) != 0) {
            vPortFree(key);
            return TC_CRYPTO_FAIL;
        }
        wc_ecc_set_rng(key, &s_blind_rng);
        rc = wc_ecc_import_private_key_ex(p_private_key, 32, (const byte *)0, 0,
                                          key, ECC_SECP256R1);
        if (rc == 0) {
            rc = wc_ecc_make_pub(key, (ecc_point *)0);
        }
        /* Export X(32)||Y(32) big-endian. Safe now — see the section-alignment fix
         * in the file header (the old sp_tohex boot hang is fixed in the linker). */
        if (rc == 0) {
            word32 xl = 32, yl = 32;
            rc = wc_ecc_export_public_raw(key, p_public_key, &xl, p_public_key + 32, &yl);
        }
        wc_ecc_free(key);
        vPortFree(key);

        if (rc == 0) {
            return TC_CRYPTO_SUCCESS;
        }
    }
    return TC_CRYPTO_FAIL;
}

/* secret: 32B = X coordinate of d*Q (big-endian). Returns TC_CRYPTO_SUCCESS/FAIL. */
int
uECC_shared_secret(const uint8_t *p_public_key, const uint8_t *p_private_key,
                   uint8_t *p_secret, uECC_Curve curve)
{
    ecc_key *kpriv, *kpub;
    byte x963[65];
    word32 outlen = 32;
    int rc, ok = TC_CRYPTO_FAIL;

    (void)curve;
    if (p_public_key == (const uint8_t *)0 || p_private_key == (const uint8_t *)0 ||
        p_secret == (uint8_t *)0) {
        return TC_CRYPTO_FAIL;
    }
    if (ensure_blind_rng() != 0) {
        return TC_CRYPTO_FAIL;
    }
    kpriv = (ecc_key *)pvPortMalloc(sizeof(ecc_key));
    kpub  = (ecc_key *)pvPortMalloc(sizeof(ecc_key));
    if (kpriv == (ecc_key *)0 || kpub == (ecc_key *)0) {
        if (kpriv) vPortFree(kpriv);
        if (kpub) vPortFree(kpub);
        return TC_CRYPTO_FAIL;
    }
    if (wc_ecc_init(kpriv) != 0 || wc_ecc_init(kpub) != 0) {
        wc_ecc_free(kpriv);
        wc_ecc_free(kpub);
        vPortFree(kpriv);
        vPortFree(kpub);
        return TC_CRYPTO_FAIL;
    }
    wc_ecc_set_rng(kpriv, &s_blind_rng);

    x963[0] = 0x04;                       /* uncompressed point */
    memcpy(x963 + 1, p_public_key, 64);

    rc = wc_ecc_import_private_key_ex(p_private_key, 32, (const byte *)0, 0,
                                      kpriv, ECC_SECP256R1);
    if (rc == 0) {
        rc = wc_ecc_import_x963(x963, sizeof(x963), kpub);
    }
    if (rc == 0) {
        rc = wc_ecc_shared_secret(kpriv, kpub, p_secret, &outlen);
    }
    if (rc == 0) {
        if (outlen < 32) {                /* left-pad to the fixed 32-byte field */
            memmove(p_secret + (32 - outlen), p_secret, outlen);
            memset(p_secret, 0, 32 - outlen);
        }
        ok = TC_CRYPTO_SUCCESS;
    }

    wc_ecc_free(kpub);
    wc_ecc_free(kpriv);
    vPortFree(kpub);
    vPortFree(kpriv);
    return ok;
}

/* >= 0 if the 64-byte (X||Y) point is a valid P-256 public key, < 0 otherwise. */
int
uECC_valid_public_key(const uint8_t *public_key, uECC_Curve curve)
{
    ecc_key *key;
    byte x963[65];
    int rc;

    (void)curve;
    if (public_key == (const uint8_t *)0) {
        return -1;
    }
    key = (ecc_key *)pvPortMalloc(sizeof(ecc_key));
    if (key == (ecc_key *)0) {
        return -1;
    }
    if (wc_ecc_init(key) != 0) {
        vPortFree(key);
        return -1;
    }
    x963[0] = 0x04;
    memcpy(x963 + 1, public_key, 64);
    rc = wc_ecc_import_x963(x963, sizeof(x963), key);
    if (rc == 0) {
        rc = wc_ecc_check_key(key);       /* on-curve, not infinity, correct order */
    }
    wc_ecc_free(key);
    vPortFree(key);
    return (rc == 0) ? 0 : -1;
}

/* --- curve_secp256r1's function-pointer fields (tinycrypt/ecc.h static const).
 * Never invoked here (we bypass the curve struct), present only so the linker
 * resolves &double_jacobian_default / &x_side_default / &vli_mmod_fast_secp256r1
 * referenced by every TU that includes ecc.h. */
void double_jacobian_default(uECC_word_t *X1, uECC_word_t *Y1, uECC_word_t *Z1,
                             uECC_Curve curve)
{
    (void)X1; (void)Y1; (void)Z1; (void)curve;
}

void x_side_default(uECC_word_t *result, const uECC_word_t *x, uECC_Curve curve)
{
    (void)result; (void)x; (void)curve;
}

void vli_mmod_fast_secp256r1(unsigned int *result, unsigned int *product)
{
    (void)result; (void)product;
}
#endif /* SM_ECC_WOLF */

/* ------------------------------------------------------------------- KAT --- */
/*
 * amebad_nimble_sm_crypto_selftest() — prove the wolfCrypt-backed primitives are
 * correct before the host runs pairing. Called once from amebad_nimble_start().
 *
 *  - AES-128-ECB     FIPS-197 known answer (via tc_aes_encrypt)
 *  - AES-CMAC        RFC 4493 vectors len 0 / 16 (via tc_cmac_*, rides on AES)
 *  - ECDH P-256      (a) symmetry: random C, ss(pubA,privC)==ss(pubC,privA)
 *                    (b) BT spec sample DHKey (report-only; anchors the output
 *                        format against an external constant)
 *  - valid_public_key  positive + negative
 */
#include "tinycrypt/cmac_mode.h"

/* BT Core Spec Vol 3, Part H, 2.3.5.6.1 P-256 sample data (big-endian) — privA/pubA
 * also match ble_sm_alg's debug key, so they're certain. */
static const uint8_t kPrivA[32] = {
    0x3f,0x49,0xf6,0xd4,0xa3,0xc5,0x5f,0x38,0x74,0xc9,0xb3,0xe3,0xd2,0x10,0x3f,0x50,
    0x4a,0xff,0x60,0x7b,0xeb,0x40,0xb7,0x99,0x58,0x99,0xb8,0xa6,0xcd,0x3c,0x1a,0xbd };
static const uint8_t kPubA[64] = {
    0x20,0xb0,0x03,0xd2,0xf2,0x97,0xbe,0x2c,0x5e,0x2c,0x83,0xa7,0xe9,0xf9,0xa5,0xb9,
    0xef,0xf4,0x91,0x11,0xac,0xf4,0xfd,0xdb,0xcc,0x03,0x01,0x48,0x0e,0x35,0x9d,0xe6,
    0xdc,0x80,0x9c,0x49,0x65,0x2a,0xeb,0x6d,0x63,0x32,0x9a,0xbf,0x5a,0x52,0x15,0x5c,
    0x76,0x63,0x45,0xc2,0x8f,0xed,0x30,0x24,0x74,0x1c,0x8e,0xd0,0x15,0x89,0xd2,0x8b };
static const uint8_t kPrivB[32] = {
    0x55,0x18,0x8b,0x3d,0x32,0xf6,0xbb,0x9a,0x90,0x0a,0xfc,0xfb,0xee,0xd4,0xe7,0x2a,
    0x59,0xcb,0x9a,0xc2,0xf1,0x9d,0x7c,0xfb,0x6b,0x4f,0xdd,0x49,0xf4,0x7f,0xc5,0xfd };
static const uint8_t kPubB[64] = {
    0x1e,0xa1,0xf0,0xf0,0x1f,0xaf,0x1d,0x96,0x09,0x59,0x22,0x84,0xf1,0x9e,0x4c,0x00,
    0x47,0xb5,0x8a,0xfd,0x86,0x15,0xa6,0x9f,0x55,0x90,0x77,0xb2,0x2f,0xaa,0xa1,0x90,
    0x4c,0x55,0xf3,0x3e,0x42,0x9d,0xad,0x37,0x73,0x56,0x70,0x3a,0x9a,0xb8,0x51,0x60,
    0x47,0x2d,0x11,0x30,0xe2,0x8e,0x36,0x76,0x5f,0x89,0xaf,0xf9,0x15,0xb1,0x21,0x4a };
static const uint8_t kDHKey[32] = {
    0xec,0x02,0x34,0xa3,0x57,0xc8,0xad,0x05,0x34,0x10,0x10,0xa6,0x0a,0x39,0x7d,0x9b,
    0x99,0x79,0x6b,0x13,0xb4,0xf8,0x66,0xf1,0x86,0x8d,0x34,0xf3,0x73,0xbf,0xa6,0x98 };

/* deterministic RNG for the KAT only (NOT used for real keys) */
extern int amebad_bench_rng_block(unsigned char *out, unsigned int sz);
static int kat_rng(uint8_t *dst, unsigned int size)
{
    return amebad_bench_rng_block(dst, size) == 0 ? 1 : 0;
}

static int eq(const uint8_t *a, const uint8_t *b, int n) { return memcmp(a, b, n) == 0; }

void
amebad_nimble_sm_crypto_selftest(void)
{
    int pass = 1;

    /* ---- AES-128-ECB (FIPS-197) ---- */
    {
        static const uint8_t key[16] = {
            0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f };
        static const uint8_t pt[16]  = {
            0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff };
        static const uint8_t ct[16]  = {
            0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a };
        struct tc_aes_key_sched_struct s;
        uint8_t out[16];
        int ok = tc_aes128_set_encrypt_key(&s, key) == TC_CRYPTO_SUCCESS &&
                 tc_aes_encrypt(out, pt, &s) == TC_CRYPTO_SUCCESS &&
                 eq(out, ct, 16);
        DiagPrintf("[SM-KAT] AES-128-ECB (wolfCrypt): %s\n\r", ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    /* ---- AES-CMAC (RFC 4493), rides on tc_aes_encrypt ---- */
    {
        static const uint8_t key[16] = {
            0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c };
        static const uint8_t m16[16] = {
            0x6b,0xc1,0xbe,0xe2,0x2e,0x40,0x9f,0x96,0xe9,0x3d,0x7e,0x11,0x73,0x93,0x17,0x2a };
        static const uint8_t t0[16]  = {
            0xbb,0x1d,0x69,0x29,0xe9,0x59,0x37,0x28,0x7f,0xa3,0x7d,0x12,0x9b,0x75,0x67,0x46 };
        static const uint8_t t16[16] = {
            0x07,0x0a,0x16,0xb4,0x6b,0x4d,0x41,0x44,0xf7,0x9b,0xdd,0x9d,0xd0,0x4a,0x28,0x7c };
        struct tc_aes_key_sched_struct sched;
        struct tc_cmac_struct state;
        uint8_t tag[16];
        int ok = 1;

        ok &= tc_cmac_setup(&state, key, &sched) != TC_CRYPTO_FAIL &&
              tc_cmac_update(&state, (const uint8_t *)0, 0) != TC_CRYPTO_FAIL &&
              tc_cmac_final(tag, &state) != TC_CRYPTO_FAIL && eq(tag, t0, 16);
        ok &= tc_cmac_setup(&state, key, &sched) != TC_CRYPTO_FAIL &&
              tc_cmac_update(&state, m16, 16) != TC_CRYPTO_FAIL &&
              tc_cmac_final(tag, &state) != TC_CRYPTO_FAIL && eq(tag, t16, 16);
        DiagPrintf("[SM-KAT] AES-CMAC (RFC4493): %s\n\r", ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    /* ---- ECDH P-256 ---- */
#if SM_ECC_WOLF
    {
        uECC_RNG_Function saved = uECC_get_rng();
        uint8_t pubC[64], privC[32], s1[32], s2[32];
        int ok_sym, ok_spec, ok_valid;

        uECC_set_rng(kat_rng);            /* KAT randomness for the ephemeral key */

        /* (a) symmetry on the real curve: ss(pubA,privC) == ss(pubC,privA) */
        ok_sym = uECC_make_key(pubC, privC, (uECC_Curve)0) == TC_CRYPTO_SUCCESS &&
                 uECC_shared_secret(kPubA, privC, s1, (uECC_Curve)0) == TC_CRYPTO_SUCCESS &&
                 uECC_shared_secret(pubC, kPrivA, s2, (uECC_Curve)0) == TC_CRYPTO_SUCCESS &&
                 eq(s1, s2, 32);
        DiagPrintf("[SM-KAT] ECDH P-256 symmetry: %s\n\r", ok_sym ? "PASS" : "FAIL");

        /* (b) BT spec sample DHKey (anchors output format/curve to an external KAT) */
        ok_spec = uECC_shared_secret(kPubB, kPrivA, s1, (uECC_Curve)0) == TC_CRYPTO_SUCCESS &&
                  eq(s1, kDHKey, 32) &&
                  uECC_shared_secret(kPubA, kPrivB, s2, (uECC_Curve)0) == TC_CRYPTO_SUCCESS &&
                  eq(s2, kDHKey, 32);
        DiagPrintf("[SM-KAT] ECDH P-256 spec DHKey: %s\n\r", ok_spec ? "PASS" : "FAIL");

        /* (c) public-key validation: good accepted, tampered rejected */
        {
            uint8_t bad[64];
            memcpy(bad, kPubA, 64);
            bad[63] ^= 0x01;
            ok_valid = uECC_valid_public_key(kPubA, (uECC_Curve)0) >= 0 &&
                       uECC_valid_public_key(bad, (uECC_Curve)0) < 0;
        }
        DiagPrintf("[SM-KAT] P-256 pubkey validate: %s\n\r", ok_valid ? "PASS" : "FAIL");

        uECC_set_rng(saved);              /* restore (NimBLE sets the real one) */
        pass &= ok_sym & ok_spec & ok_valid;
    }
#endif /* SM_ECC_WOLF */

    DiagPrintf("[SM-KAT] wolfCrypt SM crypto: %s\n\r", pass ? "ALL PASS" : "FAILURES ABOVE");
}
