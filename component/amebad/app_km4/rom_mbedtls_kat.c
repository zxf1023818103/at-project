/*
 * Known-Answer Test for the AmebaD A-cut ROM mbedTLS 2.4.0 crypto, exercised as
 * PURE SOFTWARE (use_hw_crypto_func=0 — the IPSEC HW engine is never touched).
 *
 * Proves the ROM software crypto is byte-correct against standard vectors:
 *   AES-128-ECB / AES-256-ECB (FIPS-197), AES-128-CBC round-trip,
 *   SHA-256 / SHA-1 / MD5 ("abc"), HMAC-SHA256 (RFC 4231 TC1).
 *
 * Vectors were regenerated on the host (hashlib / openssl) — not from memory.
 * Run from a task (scheduler up). Output over the log UART.
 *
 * ABI note: all context structs come from rom_mbedtls_crypto.h, which reproduces
 * the exact ROM layouts (e.g. the 344-byte RTL_HW_CRYPTO mbedtls_aes_context) and
 * _Static_asserts the sizes.
 */
#include <string.h>
#include "rom_mbedtls_crypto.h"
#include "rom_mbedtls.h"

extern int DiagPrintf(const char *fmt, ...);

static int g_pass, g_fail;

/* DiagPrintf is a limited ROM printf: %02x / %s / %d only (no %u / width). */
static void kat_report(const char *name, const unsigned char *got,
                       const unsigned char *want, int n)
{
    int i;
    if (memcmp(got, want, n) == 0) {
        g_pass++;
        DiagPrintf("[KAT] %s  PASS\r\n", name);
        return;
    }
    g_fail++;
    DiagPrintf("[KAT] %s  *** FAIL ***\r\n", name);
    DiagPrintf("[KAT]   got  ");
    for (i = 0; i < n; i++) DiagPrintf("%02x", got[i]);
    DiagPrintf("\r\n[KAT]   want ");
    for (i = 0; i < n; i++) DiagPrintf("%02x", want[i]);
    DiagPrintf("\r\n");
}

void rom_mbedtls_kat(void)
{
    DiagPrintf("\r\n[KAT] ===== ROM mbedTLS 2.4.0 software crypto KAT =====\r\n");
    rom_mbedtls_init();   /* force software path + wire ram-map calloc/free */

    g_pass = g_fail = 0;

    /* ---- AES-128-ECB (FIPS-197 C.1): key 000102..0f ---- */
    {
        static const unsigned char key[16] = {
            0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
            0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f };
        static const unsigned char pt[16] = {
            0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
            0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff };
        static const unsigned char ct[16] = {
            0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,
            0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a };
        mbedtls_aes_context c;
        unsigned char out[16], back[16];
        mbedtls_aes_init(&c);
        mbedtls_aes_setkey_enc(&c, key, 128);
        mbedtls_aes_crypt_ecb(&c, MBEDTLS_AES_ENCRYPT, pt, out);
        kat_report("AES-128-ECB enc ", out, ct, 16);
        mbedtls_aes_setkey_dec(&c, key, 128);
        mbedtls_aes_crypt_ecb(&c, MBEDTLS_AES_DECRYPT, ct, back);
        kat_report("AES-128-ECB dec ", back, pt, 16);
        mbedtls_aes_free(&c);
    }

    /* ---- AES-256-ECB (FIPS-197 C.3): key 000102..1f ---- */
    {
        static const unsigned char key[32] = {
            0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
            0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
            0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
            0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f };
        static const unsigned char pt[16] = {
            0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
            0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff };
        static const unsigned char ct[16] = {
            0x8e,0xa2,0xb7,0xca,0x51,0x67,0x45,0xbf,
            0xea,0xfc,0x49,0x90,0x4b,0x49,0x60,0x89 };
        mbedtls_aes_context c;
        unsigned char out[16];
        mbedtls_aes_init(&c);
        mbedtls_aes_setkey_enc(&c, key, 256);
        mbedtls_aes_crypt_ecb(&c, MBEDTLS_AES_ENCRYPT, pt, out);
        kat_report("AES-256-ECB enc ", out, ct, 16);
        mbedtls_aes_free(&c);
    }

    /* ---- AES-128-CBC round-trip (2 blocks): dec(enc(x)) == x ---- */
    {
        static const unsigned char key[16] = {
            0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,
            0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c };
        static const unsigned char iv0[16] = {
            0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
            0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f };
        static const unsigned char pt[32] = {
            0x6b,0xc1,0xbe,0xe2,0x2e,0x40,0x9f,0x96,
            0xe9,0x3d,0x7e,0x11,0x73,0x93,0x17,0x2a,
            0xae,0x2d,0x8a,0x57,0x1e,0x03,0xac,0x9c,
            0x9e,0xb7,0x6f,0xac,0x45,0xaf,0x8e,0x51 };
        mbedtls_aes_context c;
        unsigned char iv[16], enc[32], back[32];
        mbedtls_aes_init(&c);
        mbedtls_aes_setkey_enc(&c, key, 128);
        memcpy(iv, iv0, 16);
        mbedtls_aes_crypt_cbc(&c, MBEDTLS_AES_ENCRYPT, 32, iv, pt, enc);
        mbedtls_aes_setkey_dec(&c, key, 128);
        memcpy(iv, iv0, 16);
        mbedtls_aes_crypt_cbc(&c, MBEDTLS_AES_DECRYPT, 32, iv, enc, back);
        kat_report("AES-128-CBC rtt ", back, pt, 32);
        mbedtls_aes_free(&c);
    }

    /* ---- SHA-256("abc") ---- */
    {
        static const unsigned char want[32] = {
            0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,
            0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
            0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,
            0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad };
        unsigned char out[32];
        mbedtls_sha256((const unsigned char *)"abc", 3, out, 0);
        kat_report("SHA-256(abc)    ", out, want, 32);
    }

    /* ---- SHA-1("abc") ---- */
    {
        static const unsigned char want[20] = {
            0xa9,0x99,0x3e,0x36,0x47,0x06,0x81,0x6a,0xba,0x3e,
            0x25,0x71,0x78,0x50,0xc2,0x6c,0x9c,0xd0,0xd8,0x9d };
        unsigned char out[20];
        mbedtls_sha1((const unsigned char *)"abc", 3, out);
        kat_report("SHA-1(abc)      ", out, want, 20);
    }

    /* ---- MD5("abc") ---- */
    {
        static const unsigned char want[16] = {
            0x90,0x01,0x50,0x98,0x3c,0xd2,0x4f,0xb0,
            0xd6,0x96,0x3f,0x7d,0x28,0xe1,0x7f,0x72 };
        unsigned char out[16];
        mbedtls_md5((const unsigned char *)"abc", 3, out);
        kat_report("MD5(abc)        ", out, want, 16);
    }

    /* ---- HMAC-SHA256 (RFC 4231 TC1): key=0x0b*20, msg="Hi There" ----
     * Built manually from the raw ROM mbedtls_sha256: the ROM md-abstraction
     * layer (mbedtls_md_info_from_type) returns NULL on this part. */
    {
        static const unsigned char want[32] = {
            0xb0,0x34,0x4c,0x61,0xd8,0xdb,0x38,0x53,
            0x5c,0xa8,0xaf,0xce,0xaf,0x0b,0xf1,0x2b,
            0x88,0x1d,0xc2,0x00,0xc9,0x83,0x3d,0xa7,
            0x26,0xe9,0x37,0x6c,0x2e,0x32,0xcf,0xf7 };
        unsigned char k_ipad[64], k_opad[64], inner[32], out[32];
        unsigned char ibuf[64 + 8];     /* (key^ipad) || "Hi There" */
        unsigned char obuf[64 + 32];    /* (key^opad) || inner       */
        int i;
        memset(k_ipad, 0x36, 64);
        memset(k_opad, 0x5c, 64);
        for (i = 0; i < 20; i++) { k_ipad[i] ^= 0x0b; k_opad[i] ^= 0x0b; }
        memcpy(ibuf, k_ipad, 64);
        memcpy(ibuf + 64, "Hi There", 8);
        mbedtls_sha256(ibuf, 64 + 8, inner, 0);
        memcpy(obuf, k_opad, 64);
        memcpy(obuf + 64, inner, 32);
        mbedtls_sha256(obuf, 64 + 32, out, 0);
        kat_report("HMAC-SHA256     ", out, want, 32);
    }

    DiagPrintf("[KAT] ===== result: %d passed, %d failed =====\r\n", g_pass, g_fail);
}
