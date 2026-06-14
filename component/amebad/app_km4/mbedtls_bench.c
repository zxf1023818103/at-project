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
extern void amebad_mbedtls_port_init(void);                          /* threading-alt */
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

    amebad_mbedtls_port_init();
    cyc_init();
    DiagPrintf("\n\r[MCB] ===== Mbed TLS v3.6.4 benchmark — KM4 Cortex-M33 @200MHz (software, portable C) =====\n\r");
    DiagPrintf("[MCB] SysTick clock: %d Hz\n\r", g_hz);

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
