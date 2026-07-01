/*
 * mbedtls_user_config.h — Bouffalo BL602 / BL616 / BL616CL overrides for upstream Mbed TLS v3.6.4.
 *
 * Wired as MBEDTLS_USER_CONFIG_FILE (NOT MBEDTLS_CONFIG_FILE) by this port's CMakeLists: the
 * full upstream default config is the base and THIS file is included after it, so we only delta.
 * The base already enables everything the WiFi WPA/WPA3-SAE supplicant (lib_wlan.a) needs —
 * AES / SHA / ECP / bignum / CMAC / CTR_DRBG / ENTROPY. Each platform keeps its own config here
 * in its mbedtls_port; the shared component/mbedtls_config carries no platform deltas.
 *
 * The HACC/PKA MBEDTLS_*_ALT selections (SHA/AES/ECP + ENTROPY_HARDWARE_ALT) are NOT here —
 * they are -D compile definitions in this port's CMakeLists, next to the alt impls.
 */
#ifndef MBEDTLS_USER_CONFIG_H
#define MBEDTLS_USER_CONFIG_H

/* ---- No host OS facilities on this bare-metal target ------------------------------- */
#undef MBEDTLS_NET_C                /* no BSD sockets (net_sockets.c) */
#undef MBEDTLS_TIMING_C             /* no gettimeofday/select timing */
#undef MBEDTLS_HAVE_TIME            /* no wall-clock in mbedtls */
#undef MBEDTLS_HAVE_TIME_DATE       /* requires HAVE_TIME */
#undef MBEDTLS_FS_IO                /* app doesn't load certs from a filesystem (no fopen/<dirent.h>) */
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C /* persistent PSA keys need ITS storage */
#undef MBEDTLS_PSA_ITS_FILE_C       /* ITS-over-files needs FS_IO */

/* ---- Entropy: no /dev/urandom (MBEDTLS_ENTROPY_HARDWARE_ALT is set in the port CMakeLists;
 *      mbedtls_hardware_poll() is entropy_poll_alt.c, backed by the SEC TRNG). ------------- */
#define MBEDTLS_NO_PLATFORM_ENTROPY

#endif /* MBEDTLS_USER_CONFIG_H */
