/*
 * core_http_config.h — at-project configuration for coreHTTP (component corehttp_config).
 *
 * Active because corehttp_config does NOT define HTTP_DO_NOT_USE_CUSTOM_CONFIG; anything
 * not overridden here falls back to core_http_config_defaults.h (HTTP_USER_AGENT_VALUE,
 * the recv/send retry counts, the assert/log macros, ...). Tune those defaults here as
 * consumers need them.
 *
 * Logging: LogError / LogWarn / LogInfo / LogDebug default to no-ops in
 * core_http_config_defaults.h. Map them to the platform console here when wanted, e.g.
 *     #include <stdio.h>
 *     #define LogError( message )  do { printf( "[HTTP] " ); printf message; printf( "\r\n" ); } while( 0 )
 * (coreHTTP passes the format+args already parenthesised, so `printf message` works.)
 */
#ifndef CORE_HTTP_CONFIG_H
#define CORE_HTTP_CONFIG_H

/* Defaults are sufficient for a transport-agnostic build; add overrides below. */

#endif /* CORE_HTTP_CONFIG_H */
