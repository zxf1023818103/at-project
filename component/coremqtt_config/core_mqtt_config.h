/*
 * core_mqtt_config.h — at-project configuration for coreMQTT (component coremqtt_config).
 *
 * Active because coremqtt_config does NOT define MQTT_DO_NOT_USE_CUSTOM_CONFIG; anything
 * not overridden here falls back to core_mqtt_config_defaults.h (keep-alive interval,
 * MQTT_STATE_ARRAY_MAX_COUNT, send/recv retry counts, the assert/log macros, ...). Tune
 * those defaults here as consumers need them.
 *
 * Logging: LogError / LogWarn / LogInfo / LogDebug default to no-ops in
 * core_mqtt_config_defaults.h. Map them to the platform console here when wanted, e.g.
 *     #include <stdio.h>
 *     #define LogError( message )  do { printf( "[MQTT] " ); printf message; printf( "\r\n" ); } while( 0 )
 * (coreMQTT passes the format+args already parenthesised, so `printf message` works.)
 */
#ifndef CORE_MQTT_CONFIG_H
#define CORE_MQTT_CONFIG_H

/* Defaults are sufficient for a transport-agnostic build; add overrides below. */

#endif /* CORE_MQTT_CONFIG_H */
