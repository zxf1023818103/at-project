/*
 * sigv4_config.h — at-project configuration for SigV4 (component sigv4_config).
 *
 * Active because sigv4_config does NOT define SIGV4_DO_NOT_USE_CUSTOM_CONFIG; anything not
 * overridden here falls back to sigv4_config_defaults.h (SIGV4_PROCESSING_BUFFER_LENGTH,
 * SIGV4_MAX_HTTP_HEADER_COUNT / QUERY_COUNT, SIGV4_USE_CANONICAL_SUPPORT, the assert/log
 * macros, ...). Tune those defaults here as consumers need them — note
 * SIGV4_PROCESSING_BUFFER_LENGTH must hold the largest canonical request, so a consumer
 * signing big requests may need to raise it.
 *
 * Logging: LogError / LogWarn / LogInfo / LogDebug default to no-ops in sigv4_config_defaults.h.
 * Map them to the platform console here when wanted, e.g.
 *     #include <stdio.h>
 *     #define LogError( message )  do { printf( "[SigV4] " ); printf message; printf( "\r\n" ); } while( 0 )
 * (SigV4 passes the format+args already parenthesised, so `printf message` works.)
 */
#ifndef SIGV4_CONFIG_H
#define SIGV4_CONFIG_H

/* Defaults are sufficient; add overrides below. */

#endif /* SIGV4_CONFIG_H */
