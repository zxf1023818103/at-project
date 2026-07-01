/*
 * mbedtls_user_config.h — AmebaD KM4 overrides for upstream Mbed TLS v3.6.4.
 *
 * Wired as MBEDTLS_USER_CONFIG_FILE (NOT MBEDTLS_CONFIG_FILE) by this port's CMakeLists: the
 * full upstream default config (include/mbedtls/mbedtls_config.h) is the base and THIS file is
 * included after it, so we only delta. The base already enables everything the AmebaD stack
 * needs (PK / RSA / ECP / ECDSA / X509_CRT / SHA / AES / GCM / CMAC / CTR_DRBG / ENTROPY /
 * PEM / PSA_CRYPTO). Each platform keeps its own config here in its mbedtls_port — the shared
 * component/mbedtls_config carries no platform deltas.
 *
 * The MBEDTLS_*_ALT SELECTIONS (THREADING_ALT / ENTROPY_HARDWARE_ALT / the HW-crypto GCM/
 * CHACHAPOLY ones) are NOT here — they are -D compile definitions in this port's CMakeLists,
 * next to the alt impls (entropy_hardware_alt.c / gcm_alt.c / chachapoly_alt.c) and the
 * FreeRTOS threading-alt (component/mbedtls_config/mbedtls_threading_alt.c).
 *
 * Coexists with the ROM Mbed TLS 2.4.0 path (component/amebad/mbedtls_rom), a self-contained
 * header with no mbedtls/*.h tree — never linked into one image.
 */
#ifndef MBEDTLS_USER_CONFIG_H
#define MBEDTLS_USER_CONFIG_H

/* ---- No host OS facilities on this bare-metal target ------------------------------- */
#undef MBEDTLS_NET_C                /* no BSD sockets (net_sockets.c) */
#undef MBEDTLS_TIMING_C             /* no gettimeofday/select timing */
#undef MBEDTLS_HAVE_TIME            /* _gettimeofday is a stub here; skip wall-clock */
#undef MBEDTLS_HAVE_TIME_DATE       /* requires HAVE_TIME */
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C /* persistent PSA keys need ITS storage */
#undef MBEDTLS_PSA_ITS_FILE_C       /* ITS-over-files needs FS_IO */
/* MBEDTLS_FS_IO stays ENABLED (default): mbedtls_x509_crt_parse_file loads the AWS client cert
 * from a plain LittleFS DER file (path is env var "cert"; the key stays in TF-M ITS). fopen/
 * opendir route through the newlib VFS (component/syscall_config) onto LittleFS, and that
 * component supplies the <dirent.h> x509_crt.c needs under FS_IO — its dir is put on the mbedtls
 * include path by this port's CMakeLists. */

/* ---- Entropy: no /dev/urandom (MBEDTLS_ENTROPY_HARDWARE_ALT is set in the port CMakeLists;
 *      mbedtls_hardware_poll() is entropy_hardware_alt.c). ------------------------------- */
#define MBEDTLS_NO_PLATFORM_ENTROPY

/* ---- Threading: corePKCS11 uses mbedtls_threading_mutex_t. MBEDTLS_THREADING_ALT is set in
 *      the port CMakeLists; the mutex type is threading_alt.h + mbedtls_threading_alt.c. ---- */
#define MBEDTLS_THREADING_C

/* ---- PSA: skip the local input/output buffer copy — single address space, no trust boundary
 * between our PSA callers (picotls/corePKCS11) and mbedTLS. The default "copy caller buffers"
 * defense sized to the caller OUTPUT CAPACITY tried to calloc ~4 GB for picotls's SIZE_MAX-ish
 * QUIC capacity -> SecureFault. (Caveat: PSA in-place overlapping in/out then unsupported;
 * picotls uses separate buffers.) */
#define MBEDTLS_PSA_ASSUME_EXCLUSIVE_BUFFERS

/* ---- TLS record buffers — the KM4 NS heap is tight; default 16 KB IN + 16 KB OUT is too much.
 * IN 8 KB holds a real TLS-1.2 server's whole Certificate record (most ignore max_fragment);
 * OUT 2 KB == the max_fragment the transport requests. VARIABLE_BUFFER_LENGTH stays OFF (AWS
 * doesn't honor max_fragment, and the mid-handshake IN realloc was an intermittent hang). */
#define MBEDTLS_SSL_IN_CONTENT_LEN       8192
#define MBEDTLS_SSL_OUT_CONTENT_LEN      2048

/* ---- ECP RAM: AWS suite is ECDHE_RSA (P-256). Shrink the mul window + drop the fixed-point
 * base-point precompute — one-shot handshake ECDHE then uses less RAM (a little slower). ---- */
#define MBEDTLS_ECP_WINDOW_SIZE          2
#define MBEDTLS_ECP_FIXED_POINT_OPTIM    0

/* ---- TLS-1.2-only fallback build (TLS_BACKEND=fallback): the mbedTLS SSL stack is ONLY the 1.2
 * fallback; every 1.3 peer rides picotls (which uses mbedcrypto/x509, not the SSL layer). ---- */
#ifdef AMEBAD_MBEDTLS_TLS12_ONLY
    #undef MBEDTLS_SSL_PROTO_TLS1_3
    #undef MBEDTLS_SSL_TLS1_3_COMPATIBILITY_MODE
    #undef MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_PSK_ENABLED
    #undef MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL_ENABLED
    #undef MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_PSK_EPHEMERAL_ENABLED
    #undef MBEDTLS_SSL_KEEP_PEER_CERTIFICATE
#endif

/* ---- MAX-CIPHERSUITE experiment: the only standard key exchange the base leaves off is EC-JPAKE. */
#ifdef AMEBAD_MBEDTLS_MAX_SUITES
    #define MBEDTLS_ECJPAKE_C
    #define MBEDTLS_KEY_EXCHANGE_ECJPAKE_ENABLED
#endif

#endif /* MBEDTLS_USER_CONFIG_H */
