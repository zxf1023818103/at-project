/*
 * mbedtls_bench.c — on-target upstream Mbed TLS v3.6.4 microbenchmark for AmebaD KM4
 * (Cortex-M33 @ 200 MHz). The mbedTLS counterpart to the (removed) wolfCrypt bench — same
 * algorithm set + the same SysTick cycle counter, so the [MCB] numbers compare directly
 * against the [WCB]/[CCB] wolfCrypt + ROM-mbedTLS scores in
 * doc/amebad_crypto_rom_mbedtls_vs_wolfcrypt.md. Build: make BENCH=ON.
 *
 * Software path only (no HW accel — RTL8721D's crypto engine is secure-gated). Pure C
 * mbedTLS (no Cortex-M asm); the comparison column is the apples-to-apples "portable C"
 * one, but mbedTLS lives in XIP flash like wolfCrypt's portable build.
 */
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

#include "mbedtls/sha256.h"
#include "mbedtls/sha512.h"
#include "mbedtls/aes.h"
#include "mbedtls/gcm.h"
#include "mbedtls/chachapoly.h"
#include "mbedtls/ecp.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/rsa.h"
#include "mbedtls/bignum.h"

extern void DiagPrintf(const char *fmt, ...);
extern void mbedtls_threading_freertos_init(void);                          /* threading-alt */
extern int  mbedtls_hardware_poll(void *, unsigned char *, size_t, size_t *);

/* ---- SysTick cycle timer (DWT->CYCCNT reads 0 in this NTZ image) — verbatim from the
 *      old wolfcrypt_bench.c so the timebase matches exactly. ---- */
#define SYST_RVR (*(volatile unsigned int *)0xE000E014u)
#define SYST_CVR (*(volatile unsigned int *)0xE000E018u)
static unsigned int g_hz;
static void cyc_init(void) { g_hz = (SYST_RVR + 1u) * configTICK_RATE_HZ; }
static unsigned long long cyc_now(void)
{
    unsigned int rvr = SYST_RVR, t1, cvr, t2;
    do {
        t1  = (unsigned int) xTaskGetTickCount();
        cvr = SYST_CVR;
        t2  = (unsigned int) xTaskGetTickCount();
    } while (t1 != t2);
    return (unsigned long long) t1 * (rvr + 1u) + (rvr - cvr);
}
static void report_tp(const char *name, unsigned long long cyc, unsigned int bytes)
{
    unsigned int cpb = (unsigned int) (cyc * 100u / bytes);
    unsigned int kBps = (unsigned int) ((unsigned long long) bytes * g_hz / cyc / 1024u);
    DiagPrintf("[MCB] %s: %d.%d%d cyc/B, %d KB/s\n\r",
               name, cpb / 100, (cpb % 100) / 10, cpb % 10, kBps);
}
static void report_pk(const char *name, unsigned long long cyc, unsigned int it)
{
    unsigned int cpo = (unsigned int) (cyc / it);
    unsigned int us  = (unsigned int) (cyc * 1000000ull / it / g_hz);
    unsigned int ops = (unsigned int) ((unsigned long long) it * g_hz / cyc);
    DiagPrintf("[MCB] %s: %d cyc/op, %d us/op, %d op/s\n\r", name, cpo, us, ops);
}

static int bench_rng(void *c, unsigned char *o, size_t l)
{
    size_t ol;
    (void) c;
    return (mbedtls_hardware_poll((void *) 0, o, l, &ol) == 0) ? 0 : -1;
}

void mbedtls_bench(void)
{
    const unsigned int BUF = 4096, TOT = 512u * 1024u;   /* 512 KB through ciphers/hashes */
    unsigned char *buf;
    unsigned long long t0;
    unsigned int i, n;

    mbedtls_threading_freertos_init();
    cyc_init();
    DiagPrintf("\n\r[MCB] ===== Mbed TLS v3.6.4 benchmark — KM4 Cortex-M33 @200MHz =====\n\r");
    DiagPrintf("[MCB] SysTick clock: %d Hz\n\r", g_hz);

    /* ---- one-shot AEAD known-answer tests (crypt_and_tag — the path picotls/QUIC
     *      uses). Verifies correctness of the SOFTWARE path, or of GCM_ALT /
     *      CHACHAPOLY_ALT when built with HWGCM=ON. Inputs are stack/heap (SRAM):
     *      the IPSEC DMA can't read the const vectors in flash. ---- */
    {
        /* AES-128-GCM, NIST test case 2: zero key/IV/PT, no AAD. */
        static const unsigned char g_ct[16]  = {0x03,0x88,0xda,0xce,0x60,0xb6,0xa3,0x92,0xf3,0x28,0xc2,0xb9,0x71,0xb2,0xfe,0x78};
        static const unsigned char g_tag[16] = {0xab,0x6e,0x47,0xd4,0x2c,0xec,0x13,0xbd,0xf5,0x3a,0x67,0xb2,0x12,0x57,0xbd,0xdf};
        unsigned char gk[16] = {0}, giv[12] = {0}, gpt[16] = {0}, go[16], gt[16];
        mbedtls_gcm_context gc; int ok;
        mbedtls_gcm_init(&gc);
        mbedtls_gcm_setkey(&gc, MBEDTLS_CIPHER_ID_AES, gk, 128);
        mbedtls_gcm_crypt_and_tag(&gc, MBEDTLS_GCM_ENCRYPT, 16, giv, 12, NULL, 0, gpt, go, 16, gt);
        ok = (memcmp(go, g_ct, 16) == 0 && memcmp(gt, g_tag, 16) == 0);
        DiagPrintf("[MCB-KAT] AES-128-GCM crypt_and_tag: %s\n\r", ok ? "MATCH" : "FAIL");
        mbedtls_gcm_free(&gc);
    }
    {
        /* ChaCha20-Poly1305, RFC 8439 section 2.8.2 (114-byte message + AAD). */
        static const char cp_pt[] =
            "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
        static const unsigned char cp_nonce[12] = {0x07,0,0,0,0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47};
        static const unsigned char cp_aad[12]   = {0x50,0x51,0x52,0x53,0xc0,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7};
        static const unsigned char cp_ct[114] = {
            0xd3,0x1a,0x8d,0x34,0x64,0x8e,0x60,0xdb,0x7b,0x86,0xaf,0xbc,0x53,0xef,0x7e,0xc2,
            0xa4,0xad,0xed,0x51,0x29,0x6e,0x08,0xfe,0xa9,0xe2,0xb5,0xa7,0x36,0xee,0x62,0xd6,
            0x3d,0xbe,0xa4,0x5e,0x8c,0xa9,0x67,0x12,0x82,0xfa,0xfb,0x69,0xda,0x92,0x72,0x8b,
            0x1a,0x71,0xde,0x0a,0x9e,0x06,0x0b,0x29,0x05,0xd6,0xa5,0xb6,0x7e,0xcd,0x3b,0x36,
            0x92,0xdd,0xbd,0x7f,0x2d,0x77,0x8b,0x8c,0x98,0x03,0xae,0xe3,0x28,0x09,0x1b,0x58,
            0xfa,0xb3,0x24,0xe4,0xfa,0xd6,0x75,0x94,0x55,0x85,0x80,0x8b,0x48,0x31,0xd7,0xbc,
            0x3f,0xf4,0xde,0xf0,0x8e,0x4b,0x7a,0x9d,0xe5,0x76,0xd2,0x65,0x86,0xce,0xc6,0x4b,
            0x61,0x16};
        static const unsigned char cp_tag[16] = {0x1a,0xe1,0x0b,0x59,0x4f,0x09,0xe2,0x6a,0x7e,0x90,0x2e,0xcb,0xd0,0x60,0x06,0x91};
        unsigned char ck[32], cn[12], ca[12], co[114], ct_buf[16], *cpt;
        mbedtls_chachapoly_context cc; unsigned int j; int ok;
        for (j = 0; j < 32; j++) { ck[j] = (unsigned char)(0x80 + j); }
        memcpy(cn, cp_nonce, 12); memcpy(ca, cp_aad, 12);
        cpt = (unsigned char *) pvPortMalloc(114);
        if (cpt != NULL) {
            memcpy(cpt, cp_pt, 114);
            mbedtls_chachapoly_init(&cc);
            mbedtls_chachapoly_setkey(&cc, ck);
            mbedtls_chachapoly_encrypt_and_tag(&cc, 114, cn, ca, 12, cpt, co, ct_buf);
            ok = (memcmp(co, cp_ct, 114) == 0 && memcmp(ct_buf, cp_tag, 16) == 0);
            DiagPrintf("[MCB-KAT] ChaCha20-Poly1305 encrypt_and_tag: %s\n\r", ok ? "MATCH" : "FAIL");
            mbedtls_chachapoly_free(&cc);
            vPortFree(cpt);
        }
    }

    buf = (unsigned char *) pvPortMalloc(BUF);
    if (buf == NULL) { DiagPrintf("[MCB] OOM\n\r"); return; }
    for (i = 0; i < BUF; i++) { buf[i] = (unsigned char) i; }

    /* ---- hashes ---- */
    {
        mbedtls_sha256_context c; unsigned char md[32];
        mbedtls_sha256_init(&c); mbedtls_sha256_starts(&c, 0);
        t0 = cyc_now();
        for (n = 0; n < TOT / BUF; n++) { mbedtls_sha256_update(&c, buf, BUF); }
        mbedtls_sha256_finish(&c, md); mbedtls_sha256_free(&c);
        report_tp("SHA-256", cyc_now() - t0, TOT);
    }
    {
        mbedtls_sha512_context c; unsigned char md[64];
        mbedtls_sha512_init(&c); mbedtls_sha512_starts(&c, 0);
        t0 = cyc_now();
        for (n = 0; n < TOT / BUF; n++) { mbedtls_sha512_update(&c, buf, BUF); }
        mbedtls_sha512_finish(&c, md); mbedtls_sha512_free(&c);
        report_tp("SHA-512", cyc_now() - t0, TOT);
    }
    /* ---- symmetric ---- */
    {
        mbedtls_aes_context a; unsigned char key[16] = {0}, iv[16] = {0};
        mbedtls_aes_init(&a); mbedtls_aes_setkey_enc(&a, key, 128);
        t0 = cyc_now();
        for (n = 0; n < TOT / BUF; n++) { mbedtls_aes_crypt_cbc(&a, MBEDTLS_AES_ENCRYPT, BUF, iv, buf, buf); }
        report_tp("AES-128-CBC enc", cyc_now() - t0, TOT);
        mbedtls_aes_free(&a);
    }
    {
        mbedtls_gcm_context g; unsigned char key[16] = {0}, iv[12] = {0}, tag[16]; size_t ol;
        mbedtls_gcm_init(&g); mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 128);
        mbedtls_gcm_starts(&g, MBEDTLS_GCM_ENCRYPT, iv, 12);
        t0 = cyc_now();
        for (n = 0; n < TOT / BUF; n++) { mbedtls_gcm_update(&g, buf, BUF, buf, BUF, &ol); }
        mbedtls_gcm_finish(&g, buf, 0, &ol, tag, 16);
        report_tp("AES-128-GCM enc", cyc_now() - t0, TOT);
        mbedtls_gcm_free(&g);
    }
    {
        mbedtls_chachapoly_context c; unsigned char key[32] = {0}, nonce[12] = {0}, tag[16];
        mbedtls_chachapoly_init(&c); mbedtls_chachapoly_setkey(&c, key);
        mbedtls_chachapoly_starts(&c, nonce, MBEDTLS_CHACHAPOLY_ENCRYPT);
        t0 = cyc_now();
        for (n = 0; n < TOT / BUF; n++) { mbedtls_chachapoly_update(&c, BUF, buf, buf); }
        mbedtls_chachapoly_finish(&c, tag);
        report_tp("ChaCha20-Poly1305 enc", cyc_now() - t0, TOT);
        mbedtls_chachapoly_free(&c);
    }

    /* ---- public key ---- */
    {
        mbedtls_ecp_group grp; mbedtls_mpi d; mbedtls_ecp_point Q;
        const unsigned IT = 8;
        unsigned char h[32]; mbedtls_mpi r, s;
        for (i = 0; i < 32; i++) { h[i] = (unsigned char) i; }
        mbedtls_ecp_group_init(&grp); mbedtls_mpi_init(&d); mbedtls_ecp_point_init(&Q);
        mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
        mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);

        t0 = cyc_now();
        for (i = 0; i < IT; i++) { mbedtls_ecp_gen_keypair(&grp, &d, &Q, bench_rng, 0); }
        report_pk("ECDSA P-256 keygen", cyc_now() - t0, IT);

        t0 = cyc_now();
        for (i = 0; i < IT; i++) { mbedtls_ecdsa_sign(&grp, &r, &s, &d, h, 32, bench_rng, 0); }
        report_pk("ECDSA P-256 sign", cyc_now() - t0, IT);

        t0 = cyc_now();
        for (i = 0; i < IT; i++) { mbedtls_ecdsa_verify(&grp, h, 32, &Q, &r, &s); }
        report_pk("ECDSA P-256 verify", cyc_now() - t0, IT);

        mbedtls_mpi_free(&r); mbedtls_mpi_free(&s);
        mbedtls_ecp_point_free(&Q); mbedtls_mpi_free(&d); mbedtls_ecp_group_free(&grp);
    }
    {   /* ECDHE P-256 agree = ephemeral keygen + shared-secret compute (matches wolfCrypt) */
        mbedtls_ecp_group grp; mbedtls_mpi da, db, z; mbedtls_ecp_point Qa, Qb;
        const unsigned IT = 8;
        mbedtls_ecp_group_init(&grp); mbedtls_mpi_init(&da); mbedtls_mpi_init(&db); mbedtls_mpi_init(&z);
        mbedtls_ecp_point_init(&Qa); mbedtls_ecp_point_init(&Qb);
        mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
        mbedtls_ecdh_gen_public(&grp, &db, &Qb, bench_rng, 0);    /* fixed peer */
        t0 = cyc_now();
        for (i = 0; i < IT; i++) {
            mbedtls_ecdh_gen_public(&grp, &da, &Qa, bench_rng, 0);
            mbedtls_ecdh_compute_shared(&grp, &z, &Qb, &da, bench_rng, 0);
        }
        report_pk("ECDHE P-256 agree", cyc_now() - t0, IT);
        mbedtls_ecp_point_free(&Qa); mbedtls_ecp_point_free(&Qb);
        mbedtls_mpi_free(&da); mbedtls_mpi_free(&db); mbedtls_mpi_free(&z); mbedtls_ecp_group_free(&grp);
    }
    {   /* X25519 keygen + agree */
        mbedtls_ecp_group grp; mbedtls_mpi da, db, z; mbedtls_ecp_point Qa, Qb;
        const unsigned IT = 8;
        mbedtls_ecp_group_init(&grp); mbedtls_mpi_init(&da); mbedtls_mpi_init(&db); mbedtls_mpi_init(&z);
        mbedtls_ecp_point_init(&Qa); mbedtls_ecp_point_init(&Qb);
        mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519);
        mbedtls_ecdh_gen_public(&grp, &db, &Qb, bench_rng, 0);
        t0 = cyc_now();
        for (i = 0; i < IT; i++) { mbedtls_ecdh_gen_public(&grp, &da, &Qa, bench_rng, 0); }
        report_pk("X25519 keygen", cyc_now() - t0, IT);
        t0 = cyc_now();
        for (i = 0; i < IT; i++) { mbedtls_ecdh_compute_shared(&grp, &z, &Qb, &da, bench_rng, 0); }
        report_pk("X25519 agree", cyc_now() - t0, IT);
        mbedtls_ecp_point_free(&Qa); mbedtls_ecp_point_free(&Qb);
        mbedtls_mpi_free(&da); mbedtls_mpi_free(&db); mbedtls_mpi_free(&z); mbedtls_ecp_group_free(&grp);
    }
    {   /* RSA-2048 sign/verify (key generated once, untimed — slow on M33) */
        mbedtls_rsa_context rsa; unsigned char h[32], sig[256];
        const unsigned IT = 4;
        for (i = 0; i < 32; i++) { h[i] = (unsigned char) i; }
        mbedtls_rsa_init(&rsa);
        mbedtls_rsa_set_padding(&rsa, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_SHA256);
        DiagPrintf("[MCB] RSA-2048 keygen (one-time, slow)...\n\r");
        if (mbedtls_rsa_gen_key(&rsa, bench_rng, 0, 2048, 65537) == 0) {
            t0 = cyc_now();
            for (i = 0; i < IT; i++) {
                mbedtls_rsa_pkcs1_sign(&rsa, bench_rng, 0, MBEDTLS_MD_SHA256, 32, h, sig);
            }
            report_pk("RSA-2048 sign(priv)", cyc_now() - t0, IT);
            t0 = cyc_now();
            for (i = 0; i < IT; i++) {
                mbedtls_rsa_pkcs1_verify(&rsa, MBEDTLS_MD_SHA256, 32, h, sig);
            }
            report_pk("RSA-2048 verify(pub)", cyc_now() - t0, IT);
        } else {
            DiagPrintf("[MCB] RSA-2048 keygen FAILED\n\r");
        }
        mbedtls_rsa_free(&rsa);
    }

    DiagPrintf("[MCB] ===== benchmark done =====\n\r");
    vPortFree(buf);
}
