/*
 * defender_config.h — at-project configuration for AWS IoT Device Defender (component
 * devicedefender_config).
 *
 * Active because devicedefender_config does NOT define DEFENDER_DO_NOT_USE_CUSTOM_CONFIG;
 * anything not overridden here falls back to defender_config_defaults.h (DEFENDER_USE_LONG_KEYS
 * — short vs long metric JSON keys — and the assert/log macros).
 *
 * Logging: LogError / LogWarn / LogInfo / LogDebug default to no-ops in
 * defender_config_defaults.h. Map them to the platform console here when wanted, e.g.
 *     #include <stdio.h>
 *     #define LogError( message )  do { printf( "[Defender] " ); printf message; printf( "\r\n" ); } while( 0 )
 * (Device Defender passes the format+args already parenthesised, so `printf message` works.)
 */
#ifndef DEFENDER_CONFIG_H
#define DEFENDER_CONFIG_H

/* Defaults are sufficient; add overrides below. */

#endif /* DEFENDER_CONFIG_H */
