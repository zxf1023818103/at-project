/*
 * core_sntp_config.h — at-project configuration for coreSNTP (component coresntp_config).
 *
 * Active because coresntp_config does NOT define SNTP_DO_NOT_USE_CUSTOM_CONFIG; anything
 * not overridden here falls back to core_sntp_config_defaults.h (the assert/log macros and
 * SNTP_CONTEXT_NETWORK_BUFFER_SIZE / SNTP_DEFAULT_SERVER_PORT defaults). Tune those
 * defaults here as consumers need them.
 *
 * Logging: LogError / LogWarn / LogInfo / LogDebug default to no-ops in
 * core_sntp_config_defaults.h. Map them to the platform console here when wanted, e.g.
 *     #include <stdio.h>
 *     #define LogError( message )  do { printf( "[SNTP] " ); printf message; printf( "\r\n" ); } while( 0 )
 * (coreSNTP passes the format+args already parenthesised, so `printf message` works.)
 */
#ifndef CORE_SNTP_CONFIG_H
#define CORE_SNTP_CONFIG_H

/* Defaults are sufficient for a transport-agnostic build; add overrides below. */

#endif /* CORE_SNTP_CONFIG_H */
