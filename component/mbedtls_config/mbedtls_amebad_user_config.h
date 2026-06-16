/*
 * mbedtls_amebad_user_config.h — AmebaD KM4 overrides for upstream Mbed TLS v3.6.4.
 *
 * Wired as MBEDTLS_USER_CONFIG_FILE (NOT MBEDTLS_CONFIG_FILE): the full upstream default
 * config (include/mbedtls/mbedtls_config.h) is used as the base and THIS file is included
 * after it, so we only delta. The base already enables everything corePKCS11 (and, later,
 * picotls/ngtcp2) need: PK / RSA / ECP / ECDSA / X509_CRT / SHA / AES / CMAC / CTR_DRBG /
 * ENTROPY / PEM / PSA_CRYPTO. Here we drop the host-OS-only modules that can't build on
 * this bare-metal target, and turn on a couple of behavioural deltas. The MBEDTLS_*_ALT
 * SELECTIONS (THREADING_ALT / ENTROPY_HARDWARE_ALT / GCM_ALT / CHACHAPOLY_ALT) are NOT here —
 * they are -D compile definitions set in component/amebad/mbedtls_port/CMakeLists.txt (so the
 * HW-crypto ones can be gated on a build option), with the impls in mbedtls_threading_alt.c
 * (here) + the AmebaD port's entropy/gcm/chachapoly alt sources.
 *
 * Coexists with the ROM Mbed TLS 2.4.0 path (component/amebad/mbedtls_rom), which uses a
 * self-contained header with no mbedtls/*.h tree — the two are never linked into one image.
 */
#ifndef MBEDTLS_AMEBAD_USER_CONFIG_H
#define MBEDTLS_AMEBAD_USER_CONFIG_H

/* ---- No host OS facilities on this target ------------------------------------------- */
#undef MBEDTLS_NET_C                /* no BSD sockets (net_sockets.c) */
/* MBEDTLS_FS_IO stays ENABLED (default): mbedtls_x509_crt_parse_file loads the AWS client cert
 * from a plain LittleFS DER file (the path is env var "cert"; the key stays in TF-M ITS). fopen/
 * opendir route through the newlib VFS (component/syscall_config) onto LittleFS, and that
 * component supplies the <dirent.h> x509_crt.c needs under FS_IO (newlib's is a `#error` stub) —
 * its dir is added to the mbedtls include path in this component's CMakeLists. */
#undef MBEDTLS_TIMING_C             /* no gettimeofday/select timing */
#undef MBEDTLS_HAVE_TIME            /* _gettimeofday is a stub here; skip wall-clock */
#undef MBEDTLS_HAVE_TIME_DATE       /* requires HAVE_TIME */
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C /* persistent PSA keys need ITS storage (below) */
#undef MBEDTLS_PSA_ITS_FILE_C       /* ITS-over-files needs FS_IO */

/* ---- Threading: corePKCS11 uses mbedtls_threading_mutex_t -------------------------- */
/* MBEDTLS_THREADING_ALT is set in the port CMakeLists; the mutex type is threading_alt.h
 * (this dir) and the FreeRTOS callbacks are mbedtls_threading_alt.c — register them once via
 * mbedtls_threading_freertos_init() before any mbedTLS use. THREADING_C stays here (it is the
 * non-ALT companion that the base config does not turn on). */
#define MBEDTLS_THREADING_C

/* ---- Entropy: no /dev/urandom (MBEDTLS_ENTROPY_HARDWARE_ALT is set in the port CMakeLists;
 *      mbedtls_hardware_poll() is the AmebaD port's entropy_hardware_alt.c). --------------- */
#define MBEDTLS_NO_PLATFORM_ENTROPY

/* ---- PSA: skip the local input/output buffer copy ---------------------------------- *
 * Single address space, no trust boundary between our PSA callers (picotls/corePKCS11) and
 * mbedTLS. The default "copy caller buffers" defense calls psa_crypto_local_output_alloc()
 * sized to the caller's OUTPUT CAPACITY — picotls legitimately passes a SIZE_MAX-ish capacity
 * for QUIC header/packet protection, which then tried to calloc ~4 GB and walked into
 * secure-attributed RAM -> SecureFault. Assuming exclusive buffers removes the copy (also
 * smaller/faster). Caveat: PSA in-place (overlapping in/out) is then unsupported — picotls
 * uses separate buffers. */
#define MBEDTLS_PSA_ASSUME_EXCLUSIVE_BUFFERS

/* NB: the HW-crypto AEAD selections MBEDTLS_GCM_ALT / MBEDTLS_CHACHAPOLY_ALT (RTL8721D IPSEC
 * engine) are NOT defined here — they are gated -D defs in the port CMakeLists (make HWGCM=ON),
 * which also documents why the other *_ALT modules (SHA/AES/CCM) are deliberately left software. */

/* ---- TLS record buffers + handshake-RAM reclaim. The KM4 NS heap is tight (shared with WiFi +
 *      FreeRTOS-Plus-TCP) and the AWS handshake peak runs it to the edge — an AWS IoT TLS connect
 *      hit vApplicationMallocFailedHook (see the project_amebad_aws_iot memory). The default 16 KB
 *      IN + 16 KB OUT is far too much; trim + reclaim:
 *   - IN 8 KB: holds the AWS IoT server Certificate record (server-controlled; keep generous).
 *   - OUT 2 KB: the client Certificate (~1.3 KB) is the largest record we SEND. Paired with a 2 KB
 *     max-fragment request (transport_mbedtls.c) so 2 KB is self-consistent. Saves 2 KB at the
 *     handshake peak vs the old 4 KB — which is what closed the last malloc-fail gap.
 *   - VARIABLE_BUFFER_LENGTH: after the handshake, realloc IN/OUT down to the negotiated 2 KB
 *     fragment, freeing ~6 KB of IN slack for steady-state MQTT (which sends only small records). */
/* IN buffer = the dominant per-connection cost. AWS IoT does NOT honor the max_fragment_length
 * extension (verified: with VARIABLE_BUFFER_LENGTH on, min-ever == steady free, i.e. no post-
 * handshake shrink), so mbedTLS HOLDS this fixed buffer for the whole connection — unlike wolfSSL's
 * dynamic input buffer that shrinks after the cert (~10 KB/conn there vs ~16 KB here). It must hold
 * AWS's largest incoming record, the TLS 1.3 server cert+certverify flight (~6 KB). Probed the floor
 * on HW: 4096 -> handshake "Bad input parameters"; 6144 -> FLAKY (intermittent "Internal error" then
 * success, the flight sits right at the edge) for only ~1.3 KB saved; 8192 -> reliable. Keep 8192. */
#define MBEDTLS_SSL_IN_CONTENT_LEN       8192
#define MBEDTLS_SSL_OUT_CONTENT_LEN      2048   /* == MBEDTLS_SSL_MAX_FRAG_LEN_2048 the transport requests */
#define MBEDTLS_SSL_VARIABLE_BUFFER_LENGTH

/* (Dropping MBEDTLS_SSL_KEEP_PEER_CERTIFICATE would free the server cert chain post-handshake, but
 * the base config enables MBEDTLS_SSL_PROTO_TLS1_3 which requires it — check_config.h errors. Left
 * on; the VARIABLE_BUFFER_LENGTH reclaim above already recovers the bulk of the handshake slack.) */

/* ECP RAM: the AWS suite is ECDHE_RSA (P-256). Shrink the multiplication window and drop the
 * fixed-point base-point precompute table — the one-shot handshake ECDHE then uses less RAM (a
 * little slower, fine here). Defaults (window 4, optim on) are heavier than this part can spare. */
#define MBEDTLS_ECP_WINDOW_SIZE          2
#define MBEDTLS_ECP_FIXED_POINT_OPTIM    0

#endif /* MBEDTLS_AMEBAD_USER_CONFIG_H */
