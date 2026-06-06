/*
 * wolfcrypt_bench.c — on-target wolfCrypt microbenchmark for AmebaD KM4
 * (Cortex-M33 @ 200 MHz). Puts real numbers behind
 * doc/amebad_wolfssl_hwaccel_eval.md.
 *
 * Times the handshake-relevant public-key ops (ECC P-256, X25519, RSA-2048)
 * and the record-layer bulk ops (AES-128-GCM, SHA-256/512, ChaCha20-Poly1305)
 * using the Cortex-M DWT cycle counter. Reports cycles/op (clock-independent)
 * plus derived us/op, ops/s and KB/s at 200 MHz.
 *
 * Build A: SP Cortex-M asm (PK) + portable-C symmetric.
 * Build B: + Thumb2 symmetric asm (-DWOLFSSL_AMEBAD_ARMASM on amebad_wolfssl).
 * The banner prints which symmetric tier is active so the two flashes are
 * self-labeling.
 */
#include "FreeRTOS.h"
#include "task.h"

#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/sha256.h>
#include <wolfssl/wolfcrypt/sha512.h>
#include <wolfssl/wolfcrypt/aes.h>
#include <wolfssl/wolfcrypt/chacha20_poly1305.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/curve25519.h>
#include <wolfssl/wolfcrypt/rsa.h>
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/asn_public.h>
#include <wolfssl/certs_test.h>

#include <stddef.h>
extern void DiagPrintf(const char *fmt, ...);

/* wolfSSL XMALLOC_USER hooks -> FreeRTOS heap (avoids newlib malloc/_sbrk on
 * this -nostdlib image). XREALLOC is never referenced in this wolfCrypt config. */
void* XMALLOC(size_t n, void* heap, int type)
{ (void)heap; (void)type; return pvPortMalloc(n); }
void  XFREE(void* p, void* heap, int type)
{ (void)heap; (void)type; if (p) vPortFree(p); }
void* XREALLOC(void* p, size_t n, void* heap, int type)
{ (void)p; (void)n; (void)heap; (void)type; DiagPrintf("[WCB] XREALLOC unexpected\n\r"); return NULL; }

/* ---- High-res cycle timer via SysTick ----
 * The DWT cycle counter (0xE0001004) reads 0 from this driver-only NTZ image
 * (secure-gated, like the HW crypto engine). SysTick, by contrast, is the
 * live FreeRTOS tick source (non-secure, CPU-clocked), so combine the tick
 * count with the sub-tick SysTick value for a CPU-cycle-resolution counter.
 *   cycles = tick*(RVR+1) + (RVR - CVR)         (RVR = reload, CVR = current)
 * SysTick frequency is derived: (RVR+1) * configTICK_RATE_HZ  (~200 MHz). */
#define SYST_RVR (*(volatile unsigned int*)0xE000E014u)   /* reload */
#define SYST_CVR (*(volatile unsigned int*)0xE000E018u)   /* current */
static unsigned int g_hz;               /* derived SysTick (== CPU) frequency */

static void cyc_init(void) { g_hz = (SYST_RVR + 1u) * configTICK_RATE_HZ; }

static unsigned long long cyc_now(void)
{
    unsigned int rvr = SYST_RVR, t1, cvr, t2;
    do {                                /* re-read if a tick rolled mid-sample */
        t1  = (unsigned int)xTaskGetTickCount();
        cvr = SYST_CVR;
        t2  = (unsigned int)xTaskGetTickCount();
    } while (t1 != t2);
    return (unsigned long long)t1 * (rvr + 1u) + (rvr - cvr);
}

/* ---- RNG for the benchmark (CUSTOM_RAND_GENERATE_BLOCK). Not crypto-grade;
 *      benchmark only — a fast xorshift so keygen/blinding have entropy. ---- */
static unsigned int s_rng = 0xA5C3F19Du;
int amebad_bench_rng_block(unsigned char* out, unsigned int sz)
{
    unsigned int i;
    for (i = 0; i < sz; i++) {
        s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
        out[i] = (unsigned char)(s_rng >> 24);
    }
    return 0;
}

/* ---- reporting. DiagPrintf is a limited ROM printf: %s/%d/%c only, NO width
 *      or flags (%-22s / %02d corrupt the varargs). Compute in 64-bit, emit %d. */
static void report_tp(const char* name, unsigned long long cycles, unsigned int bytes)
{
    unsigned int cpb_x100 = (unsigned int)(cycles * 100u / bytes);
    unsigned int kBps = (unsigned int)((unsigned long long)bytes * g_hz / cycles / 1024u);
    DiagPrintf("[WCB] %s: %d.%d%d cyc/B, %d KB/s\n\r",
               name, cpb_x100/100, (cpb_x100%100)/10, cpb_x100%10, kBps);
}
static void report_pk(const char* name, unsigned long long cycles, unsigned int iters)
{
    unsigned int cpo = (unsigned int)(cycles / iters);
    unsigned int us  = (unsigned int)(cycles * 1000000ull / iters / g_hz);
    unsigned int ops = (unsigned int)((unsigned long long)iters * g_hz / cycles);
    DiagPrintf("[WCB] %s: %d cyc/op, %d us/op, %d op/s\n\r", name, cpo, us, ops);
}

/* ---------------------------------------------------------------------------- */
void wolfcrypt_bench(void)
{
    const unsigned int BUF = 4096, TOT = 512u*1024u;   /* 512 KB through ciphers */
    unsigned char* buf;
    unsigned long long t0;
    unsigned int i, n;
    int ret;
    WC_RNG rng;

    cyc_init();
    DiagPrintf("\n\r[WCB] ===== wolfCrypt v5.9.1 benchmark — KM4 Cortex-M33 =====\n\r");
    DiagPrintf("[WCB] SysTick clock: %d Hz (timer base)\n\r", g_hz);
    DiagPrintf("[WCB] symmetric/hash: %s   |   PK: %s\n\r",
#ifdef WOLFSSL_AMEBAD_ARMASM
               "Thumb2 ASM",
#else
               "portable C",
#endif
#ifdef WOLFSSL_AMEBAD_NO_SP_ASM
               "portable C");
#else
               "SP Cortex-M ASM");
#endif

    buf = (unsigned char*)pvPortMalloc(BUF);
    if (!buf) { DiagPrintf("[WCB] OOM buf\n\r"); return; }
    for (i = 0; i < BUF; i++) buf[i] = (unsigned char)i;

    if (wc_InitRng(&rng) != 0) { DiagPrintf("[WCB] RNG init fail\n\r"); }

    /* ---------- Hash throughput ---------- */
    {
        wc_Sha256 sha; unsigned char dig[32];
        wc_InitSha256(&sha);
        t0 = cyc_now();
        for (n = 0; n < TOT/BUF; n++) wc_Sha256Update(&sha, buf, BUF);
        wc_Sha256Final(&sha, dig);
        report_tp("SHA-256", cyc_now()-t0, TOT);
    }
    {
        wc_Sha512 sha; unsigned char dig[64];
        wc_InitSha512(&sha);
        t0 = cyc_now();
        for (n = 0; n < TOT/BUF; n++) wc_Sha512Update(&sha, buf, BUF);
        wc_Sha512Final(&sha, dig);
        report_tp("SHA-512", cyc_now()-t0, TOT);
    }

    /* ---------- AES-128-GCM throughput ---------- */
    {
        Aes aes; unsigned char key[16]={0}, iv[12]={0}, tag[16], *ct;
        ct = (unsigned char*)pvPortMalloc(BUF);
        if (ct && wc_AesGcmSetKey(&aes, key, 16) == 0) {
            t0 = cyc_now();
            for (n = 0; n < TOT/BUF; n++)
                wc_AesGcmEncrypt(&aes, ct, buf, BUF, iv, 12, tag, 16, NULL, 0);
            report_tp("AES-128-GCM enc", cyc_now()-t0, TOT);
        }
        if (ct) vPortFree(ct);
    }

    /* ---------- ChaCha20-Poly1305 throughput ---------- */
    {
        unsigned char key[32]={0}, iv[12]={0}, tag[16], *ct;
        ct = (unsigned char*)pvPortMalloc(BUF);
        if (ct) {
            t0 = cyc_now();
            for (n = 0; n < TOT/BUF; n++)
                wc_ChaCha20Poly1305_Encrypt(key, iv, NULL, 0, buf, BUF, ct, tag);
            report_tp("ChaCha20-Poly1305 enc", cyc_now()-t0, TOT);
            vPortFree(ct);
        }
    }

    /* ---------- ECC P-256 (keygen / ECDHE / sign / verify) ---------- */
    {
        const int ITER = 10;
        ecc_key a, b; unsigned char sig[80], dig[32]={1,2,3,4};
        unsigned char secret[32]; word32 slen, siglen; int vr;

        /* keygen */
        t0 = cyc_now();
        for (i = 0; i < ITER; i++) {
            wc_ecc_init(&a);
            if (wc_ecc_make_key(&rng, 32, &a) != 0) { DiagPrintf("[WCB] ecc kg fail\n\r"); }
            if (i < ITER-1) wc_ecc_free(&a);
        }
        report_pk("ECDSA P-256 keygen", cyc_now()-t0, ITER);

        /* ECDHE shared secret (a x b) */
        wc_ecc_init(&b); wc_ecc_make_key(&rng, 32, &b);
        t0 = cyc_now();
        for (i = 0; i < ITER; i++) { slen = sizeof secret; wc_ecc_shared_secret(&a, &b, secret, &slen); }
        report_pk("ECDHE P-256 agree", cyc_now()-t0, ITER);

        /* ECDSA sign / verify */
        t0 = cyc_now();
        for (i = 0; i < ITER; i++) { siglen = sizeof sig; wc_ecc_sign_hash(dig, 32, sig, &siglen, &rng, &a); }
        report_pk("ECDSA P-256 sign", cyc_now()-t0, ITER);
        t0 = cyc_now();
        for (i = 0; i < ITER; i++) wc_ecc_verify_hash(sig, siglen, dig, 32, &vr, &a);
        report_pk("ECDSA P-256 verify", cyc_now()-t0, ITER);
        wc_ecc_free(&a); wc_ecc_free(&b);
    }

    /* ---------- X25519 (keygen / shared secret) ---------- */
    {
        const int ITER = 10;
        curve25519_key a, b; unsigned char secret[32]; word32 slen;
        t0 = cyc_now();
        for (i = 0; i < ITER; i++) {
            wc_curve25519_init(&a);
            wc_curve25519_make_key(&rng, 32, &a);
            if (i < ITER-1) wc_curve25519_free(&a);
        }
        report_pk("X25519 keygen", cyc_now()-t0, ITER);
        wc_curve25519_init(&b); wc_curve25519_make_key(&rng, 32, &b);
        t0 = cyc_now();
        for (i = 0; i < ITER; i++) { slen = sizeof secret; wc_curve25519_shared_secret(&a, &b, secret, &slen); }
        report_pk("X25519 agree", cyc_now()-t0, ITER);
        wc_curve25519_free(&a); wc_curve25519_free(&b);
    }

    /* ---------- RSA-2048 (verify public / sign private) ---------- */
    {
        const int ITER = 5;
        RsaKey key; word32 idx = 0; unsigned char out[256], sig[256]; word32 olen;
        wc_InitRsaKey(&key, NULL);
        wc_RsaSetRNG(&key, &rng);
        ret = wc_RsaPrivateKeyDecode(client_key_der_2048, &idx, &key,
                                     sizeof_client_key_der_2048);
        if (ret == 0) {
            unsigned char msg[32] = {9,8,7,6};
            /* private op (sign) — the heavy one */
            t0 = cyc_now();
            for (i = 0; i < ITER; i++) { olen = wc_RsaSSL_Sign(msg, 32, sig, sizeof sig, &key, &rng); }
            report_pk("RSA-2048 sign(priv)", cyc_now()-t0, ITER);
            /* public op (verify) — the handshake cert-check cost */
            t0 = cyc_now();
            for (i = 0; i < ITER; i++) wc_RsaSSL_Verify(sig, olen, out, sizeof out, &key);
            report_pk("RSA-2048 verify(pub)", cyc_now()-t0, ITER);
        } else {
            DiagPrintf("[WCB] RSA key decode fail %d\n\r", ret);
        }
        wc_FreeRsaKey(&key);
    }

    wc_FreeRng(&rng);
    vPortFree(buf);
    DiagPrintf("[WCB] ===== benchmark done =====\n\r");
}
