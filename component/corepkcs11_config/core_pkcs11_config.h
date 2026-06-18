/*
 * core_pkcs11_config.h — at-project configuration for corePKCS11 (component corepkcs11_config).
 *
 * Anything not overridden here falls back to core_pkcs11_config_defaults.h (max sessions /
 * objects / label length, the default user PIN + JITP labels, the assert/log macros, and
 * pkcs11configPKCS11_MALLOC/FREE which default to libc malloc/free). Tune those defaults
 * here as a consumer needs them.
 *
 * Logging: LogError / LogWarn / LogInfo / LogDebug default to no-ops in
 * core_pkcs11_config_defaults.h. Map them to the platform console here when wanted, e.g.
 *     #include <stdio.h>
 *     #define LogError( message )  do { printf( "[PKCS11] " ); printf message; printf( "\r\n" ); } while( 0 )
 * (corePKCS11 passes the format+args already parenthesised, so `printf message` works.)
 *
 * NB: the only crypto backend corePKCS11 ships is the mbedTLS port
 * (source/portable/mbedtls/core_pkcs11_mbedtls.c). It needs an mbedTLS (>= 2.28 / 3.x) built
 * with MBEDTLS_THREADING_C (mbedtls_threading_mutex_t) — wire a threading backend
 * (mbedtls_threading_set_alt over FreeRTOS, or PTHREAD) in the consumer's mbedTLS config.
 * The PKCS #11 PAL (object storage: PKCS11_PAL_*) is consumer-supplied — the bundled PALs
 * are POSIX/Windows only and are NOT built into the corepkcs11 lib.
 */
#ifndef CORE_PKCS11_CONFIG_H
#define CORE_PKCS11_CONFIG_H

/* Defaults are sufficient to compile the library; add overrides below. */

#endif /* CORE_PKCS11_CONFIG_H */
