/*
 * mbedtls_amebad_user_config.h — AmebaD KM4 overrides for upstream Mbed TLS v3.6.4.
 *
 * Wired as MBEDTLS_USER_CONFIG_FILE (NOT MBEDTLS_CONFIG_FILE): the full upstream default
 * config (include/mbedtls/mbedtls_config.h) is used as the base and THIS file is included
 * after it, so we only delta. The base already enables everything corePKCS11 (and, later,
 * picotls/ngtcp2) need: PK / RSA / ECP / ECDSA / X509_CRT / SHA / AES / CMAC / CTR_DRBG /
 * ENTROPY / PEM / PSA_CRYPTO. Here we (1) drop the host-OS-only modules that can't build on
 * this bare-metal target, and (2) turn on threading + a hardware entropy source.
 *
 * Coexists with the ROM Mbed TLS 2.4.0 path (component/amebad/mbedtls_rom), which uses a
 * self-contained header with no mbedtls/*.h tree — the two are never linked into one image.
 */
#ifndef MBEDTLS_AMEBAD_USER_CONFIG_H
#define MBEDTLS_AMEBAD_USER_CONFIG_H

/* ---- No host OS facilities on this target ------------------------------------------- */
#undef MBEDTLS_NET_C                /* no BSD sockets (net_sockets.c) */
#undef MBEDTLS_FS_IO                /* no filesystem */
#undef MBEDTLS_TIMING_C             /* no gettimeofday/select timing */
#undef MBEDTLS_HAVE_TIME            /* _gettimeofday is a stub here; skip wall-clock */
#undef MBEDTLS_HAVE_TIME_DATE       /* requires HAVE_TIME */
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C /* persistent PSA keys need ITS storage (below) */
#undef MBEDTLS_PSA_ITS_FILE_C       /* ITS-over-files needs FS_IO */

/* ---- Threading: corePKCS11 uses mbedtls_threading_mutex_t -------------------------- */
/* ALT = we supply the mutex type (threading_alt.h) + the four callbacks over FreeRTOS
 * (mbedtls_threading_set_alt in mbedtls_amebad_port.c, call amebad_mbedtls_port_init early). */
#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_ALT

/* ---- Entropy: no /dev/urandom — supply mbedtls_hardware_poll() --------------------- */
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT

/* ---- PSA: skip the local input/output buffer copy ---------------------------------- *
 * Single address space, no trust boundary between our PSA callers (picotls/corePKCS11) and
 * mbedTLS. The default "copy caller buffers" defense calls psa_crypto_local_output_alloc()
 * sized to the caller's OUTPUT CAPACITY — picotls legitimately passes a SIZE_MAX-ish capacity
 * for QUIC header/packet protection, which then tried to calloc ~4 GB and walked into
 * secure-attributed RAM -> SecureFault. Assuming exclusive buffers removes the copy (also
 * smaller/faster). Caveat: PSA in-place (overlapping in/out) is then unsupported — picotls
 * uses separate buffers. */
#define MBEDTLS_PSA_ASSUME_EXCLUSIVE_BUFFERS

/* ---- HW crypto *_ALT survey (opt-in via the AMEBAD_MBEDTLS_HW_GCM build option) ----- *
 * Which mbedTLS *_ALT modules the RTL8721D IPSEC engine can usefully back. Enabling one
 * means mbedcrypto drops that module's software impl and links our gcm_alt.c-style HW
 * version. After surveying the engine (ROM symbol table + fwlib/crypto), GCM is the only
 * one that maps cleanly; the rest are deliberately left software:
 *
 *   MBEDTLS_GCM_ALT       ENABLED. The HW one-shot rtl_crypto_aes_gcm_{init,encrypt,
 *                         decrypt} are in ROM (clean addresses, no SDK source, no engine-
 *                         struct access). ~4 cyc/B / 48 MB/s vs 465 cyc/B / 419 KB/s SW —
 *                         the QUIC AES-GCM record-cipher bottleneck. gcm_alt.{h,c}.
 *   MBEDTLS_CHACHAPOLY_ALT  ENABLED. No one-shot wrapper in ROM (only CRYPTO_chacha_20 /
 *                         CRYPTO_poly1305_* primitives), so chachapoly_alt.c drives the ROM
 *                         generic-cipher path (CRYPTO_CipherInit/EncryptAD + the ROM
 *                         crypto_engine) inline — no SDK source vendored. Cold path: with HW
 *                         AES-GCM we PREFER GCM in cipher negotiation, so this only serves a
 *                         ChaCha-only peer. chachapoly_alt.{h,c}.
 *   MBEDTLS_SHA256/512_ALT  skipped. HW SHA shares the single-context engine, but mbedTLS
 *                         clones / keeps multiple concurrent SHA contexts alive (the TLS
 *                         transcript hash), which one HW context cannot hold; and SW SHA-256
 *                         is already ~50 cyc/B (~ the Thumb2 asm). Net loss.
 *   MBEDTLS_AES_ALT       skipped. That replaces the 16-byte BLOCK cipher; a DMA setup per
 *                         block is an anti-pattern. The HW win is at the mode/AEAD level
 *                         (GCM), not the block level.
 *   CCM / DES / MD5 *_ALT  n/a. Not in ROM and/or not used by TLS 1.3 / QUIC.
 *
 * NSPE-direct works under TF-M's SAU (reference_amebad_hw_crypto). Off by default so the
 * software [MCB] bench and non-QUIC images are unaffected. */
#if defined(AMEBAD_MBEDTLS_HW_GCM)
#define MBEDTLS_GCM_ALT
#define MBEDTLS_CHACHAPOLY_ALT
#endif

#endif /* MBEDTLS_AMEBAD_USER_CONFIG_H */
