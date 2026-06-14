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

/* ---- HW AES-GCM (opt-in via the AMEBAD_MBEDTLS_HW_GCM build option) ----------------- *
 * Route the whole GCM module to the RTL8721D IPSEC engine (gcm_alt.{h,c}) instead of the
 * software GHASH — measured ~4 cyc/B / 48 MB/s vs 465 cyc/B / 419 KB/s, the QUIC AES-GCM
 * bottleneck. NSPE-direct works under TF-M's SAU (reference_amebad_hw_crypto). Off by
 * default so the software bench / non-QUIC images are unaffected; enable per build. */
#if defined(AMEBAD_MBEDTLS_HW_GCM)
#define MBEDTLS_GCM_ALT
#endif

#endif /* MBEDTLS_AMEBAD_USER_CONFIG_H */
