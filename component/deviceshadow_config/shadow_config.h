/*
 * shadow_config.h — at-project configuration for AWS IoT Device Shadow (component
 * deviceshadow_config).
 *
 * Active because deviceshadow_config does NOT define SHADOW_DO_NOT_USE_CUSTOM_CONFIG; anything
 * not overridden here falls back to shadow_config_defaults.h (just the assert/log macros).
 *
 * Logging: LogError / LogWarn / LogInfo / LogDebug default to no-ops in shadow_config_defaults.h.
 * Map them to the platform console here when wanted, e.g.
 *     #include <stdio.h>
 *     #define LogError( message )  do { printf( "[Shadow] " ); printf message; printf( "\r\n" ); } while( 0 )
 * (Device Shadow passes the format+args already parenthesised, so `printf message` works.)
 */
#ifndef SHADOW_CONFIG_H
#define SHADOW_CONFIG_H

/* Defaults are sufficient; add overrides below. */

#endif /* SHADOW_CONFIG_H */
