/* user_settings.h — wolfCrypt bare-metal config for AmebaD KM4 (Cortex-M33).
 *
 * On-target crypto benchmark config. Adapted from wolfSSL's
 * IDE/SimplicityStudio/user_settings.h (a Cortex-M SP-asm template).
 *
 * Target: ARMv8-M Mainline + DSP + FPU (-march=armv8-m.main+dsp,
 * -mfpu=fpv5-sp-d16, -mfloat-abi=hard), FreeRTOS, -Os.
 *
 * Acceleration knobs (see doc/amebad_wolfssl_hwaccel_eval.md):
 *   - Public key (handshake): WOLFSSL_SP_MATH_ALL + WOLFSSL_SP_ASM +
 *     WOLFSSL_SP_ARM_CORTEX_M_ASM  -> sp_int.c (generic) + sp_cortexm.c
 *     (dedicated P-256 / RSA-2048/3072 Cortex-M assembly).
 *   - Symmetric/hash (record layer): Thumb2 assembly via WOLFSSL_ARMASM +
 *     WOLFSSL_ARMASM_THUMB2 + WOLFSSL_ARMASM_INLINE. Gated behind
 *     WOLFSSL_AMEBAD_ARMASM below so Build A (PK-asm only, portable-C
 *     symmetric) and Build B (+Thumb2 symmetric asm) differ by one -D.
 *
 * NB: ARMv8-M (not ARMv7-M) -> WOLFSSL_ARM_ARCH_7M is NOT auto-set, so
 * wolfSSL does NOT force WOLFSSL_SP_NO_UMAAL; +dsp provides UMAAL -> used.
 */
#ifndef WOLFSSL_USER_SETTINGS_H
#define WOLFSSL_USER_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Platform ---- */
#define WOLFSSL_GENERAL_ALIGNMENT 4
#define SIZEOF_LONG_LONG 8
#define WOLFSSL_SMALL_STACK          /* >100 B locals from heap (M33 stack is tight) */
#define WOLFSSL_USER_IO              /* no built-in socket I/O (QUIC feeds crypto via callbacks) */
#ifndef AMEBAD_WOLFSSL_TLS
    #define WOLFCRYPT_ONLY           /* default: wolfCrypt only, no TLS layer (bench / NimBLE SM) */
#endif
#define WOLFSSL_IGNORE_FILE_WARN     /* quiet "file built but not needed" */
#define SINGLE_THREADED              /* benchmark runs in one task */

/* Heap: route XMALLOC/XFREE/XREALLOC to the FreeRTOS heap (defined in
 * app_km4/wolfcrypt_bench.c). Avoids newlib malloc -> _sbrk on this
 * -nostdlib image. (realloc is never referenced in this config -> stub.) */
#define XMALLOC_USER

/* ---- Math: SP + Cortex-M assembly (the handshake accelerator) ---- */
#define WOLFSSL_HAVE_SP_RSA
#define WOLFSSL_HAVE_SP_DH
#define WOLFSSL_HAVE_SP_ECC
#define WOLFSSL_SP_MATH_ALL          /* SP math for all sizes; drops integer.c/tfm.c */
#define WOLFSSL_SP_SMALL             /* compact asm variant (fits flash); still asm */
#ifndef WOLFSSL_AMEBAD_NO_SP_ASM     /* define (-D) for the portable-C baseline build */
    #define WOLFSSL_SP_ASM
    #define WOLFSSL_SP_ARM_CORTEX_M_ASM
#endif

/* ---- Symmetric/hash Thumb2 asm (Build B) ----
 * Define WOLFSSL_AMEBAD_ARMASM (-D on the wolfssl target) to switch the
 * record-layer crypto from portable C to hand-written Thumb2 assembly. */
#ifdef WOLFSSL_AMEBAD_ARMASM
    #define WOLFSSL_ARMASM
    #define WOLFSSL_ARMASM_THUMB2
    #define WOLFSSL_ARMASM_INLINE        /* use the *_c.c inline-asm (compile as C) */
    #define WOLFSSL_ARMASM_NO_HW_CRYPTO  /* M33 has no ARMv8-A AES/SHA instructions */
    #define WOLFSSL_ARMASM_NO_NEON       /* M33 has no NEON -> use the _base transforms */
    #define WOLFSSL_NO_HASH_RAW
#endif

/* ---- RSA ---- */
#undef  NO_RSA
#define WC_RSA_BLINDING
#define WC_RSA_PSS

/* ---- DH: off (not benchmarked) ---- */
#define NO_DH

/* ---- ECC (P-256 only) ---- */
#define HAVE_ECC
#define ECC_USER_CURVES
#undef  NO_ECC256
#define ECC_SHAMIR
#define ECC_TIMING_RESISTANT

/* ---- AES ---- */
#undef  NO_AES
#define HAVE_AES_CBC
#define WOLFSSL_AES_COUNTER
#define WOLFSSL_AES_DIRECT           /* single-block ECB (wc_AesEncryptDirect) — NimBLE SM e()/CMAC */
#define HAVE_AESGCM
#define GCM_TABLE_4BIT               /* ~256 B table, good speed/size on M (eval §5) */

/* ---- ChaCha20-Poly1305 ---- */
#define HAVE_CHACHA
#define HAVE_POLY1305
#define HAVE_ONE_TIME_AUTH

/* ---- Curve25519 (X25519 KEX) ---- */
#define HAVE_CURVE25519

/* ---- Hashes ---- */
#undef  NO_SHA                       /* SHA-1 (cert path) */
#undef  NO_SHA256
#define WOLFSSL_SHA512               /* SHA-384/512 throughput + thumb2 asm */
#define WOLFSSL_SHA384

/* ---- RNG: bypass HASH-DRBG, use a benchmark block source ---- */
#define WC_NO_HASHDRBG
extern int amebad_bench_rng_block(unsigned char* output, unsigned int sz);
#undef  CUSTOM_RAND_GENERATE_BLOCK
#define CUSTOM_RAND_GENERATE_BLOCK amebad_bench_rng_block

/* ---- Test key material (RSA-2048 / ECC P-256 DER buffers, no filesystem) ---- */
#define USE_CERT_BUFFERS_2048
#define USE_CERT_BUFFERS_256
#define NO_ASN_TIME                  /* importing keys, not checking cert validity */

/* ---- TLS 1.3 + QUIC (gated: -DAMEBAD_WOLFSSL_TLS) ----
 * Enables the full TLS 1.3 record/handshake layer plus wolfSSL's QUIC API
 * (src/quic.c) and the OpenSSL EVP compatibility layer that ngtcp2's
 * crypto/wolfssl backend calls (wolfSSL_EVP_*, wolfSSL_quic_*). TLS 1.2 is
 * compiled out — QUIC mandates TLS 1.3, so there is no 1.2 code path. */
#ifdef AMEBAD_WOLFSSL_TLS
    #define WOLFSSL_TLS13
    #define WOLFSSL_NO_TLS12         /* QUIC is TLS1.3-only — drop the 1.2 record layer */
    #define WOLFSSL_NO_SOCK          /* no OS sockets: ngtcp2 feeds crypto via callbacks */
    #define WOLFSSL_QUIC             /* src/quic.c: wolfSSL_set_quic_method + secret cbs */
    #define OPENSSL_EXTRA            /* EVP_* compat layer ngtcp2's backend links against */
    #define HAVE_EX_DATA             /* wolfSSL_set_app_data: ngtcp2 attaches its conn ref */

    #define HAVE_TLS_EXTENSIONS      /* required by TLS 1.3 */
    #define HAVE_SUPPORTED_CURVES    /* key_share / supported_groups */
    #define HAVE_HKDF                /* TLS 1.3 key schedule (kdf.c) */
    #define HAVE_ALPN                /* QUIC requires ALPN */
    /* NB: HAVE_SESSION_TICKET is force-disabled by settings.h because NO_ASN_TIME
     * is set (tickets need timestamps). ngtcp2's configure_client_context calls
     * wolfSSL_CTX_UseSessionTicket regardless; quic_test.c supplies a no-op stub.
     * Resumption isn't needed for the handshake smoke test. */
    #define HAVE_AEAD
    #define HAVE_AES_ECB             /* header protection: wolfSSL_EVP_aes_128/256_ecb */
    #define HAVE_EXTENDED_MASTER
    #define WOLFSSL_DTLS_NO_HVR_ON_RESUME /* unused; quiets a feature combo */

    /* With WOLFSSL_NO_SOCK the X509 host-IP-match path (MatchIpName in x509.c,
     * only reached via peer verification — we use SSL_VERIFY_NONE) still
     * references inet_pton/AF_INET. Stub them; the path is never taken. */
    #define WOLFSSL_IP4 2
    #define WOLFSSL_IP6 10
    #define XINET_PTON(a, b, c) (0)
    #define XINET_NTOP(a, b, c, d) (NULL)

    /* No filesystem/time/peer-verify for the on-target handshake smoke test:
     * the client runs with SSL_VERIFY_NONE, so cert chain time/CA checks are
     * skipped (NO_ASN_TIME stays defined below). */
#endif

/* ---- Trim ---- */
#define NO_FILESYSTEM
#define NO_WRITEV
#define NO_MAIN_DRIVER
#define NO_DEV_RANDOM
#define NO_OLD_TLS
#define NO_PSK
#define NO_DSA
#define NO_RC4
#define NO_MD4
#define NO_MD5
#define NO_DES3
#define NO_PWDBASED
#define NO_ERROR_STRINGS

#ifdef __cplusplus
}
#endif

#endif /* WOLFSSL_USER_SETTINGS_H */
