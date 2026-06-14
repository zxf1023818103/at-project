/*
 * ota_config.h — at-project configuration for the AWS IoT OTA Agent (component ota_config).
 *
 * Active because ota_config does NOT define OTA_DO_NOT_USE_CUSTOM_CONFIG; anything not overridden
 * here falls back to ota_config_defaults.h (otaconfigMAX_NUM_BLOCKS_REQUEST,
 * otaconfigFILE_BLOCK_SIZE, otaconfigMAX_NUM_OTA_DATA_BUFFERS, the AGENT task stack/priority,
 * otaconfigOTA_FILE_TYPE, the self-test/activate hooks, and the assert/log macros). Tune those
 * defaults here as the application needs them — block size / buffer count drive RAM use.
 *
 * Logging: LogError / LogWarn / LogInfo / LogDebug default to no-ops in ota_config_defaults.h.
 * Map them to the platform console here when wanted, e.g.
 *     #include <stdio.h>
 *     #define LogError( message )  do { printf( "[OTA] " ); printf message; printf( "\r\n" ); } while( 0 )
 * (the OTA Agent passes the format+args already parenthesised, so `printf message` works.)
 */
#ifndef OTA_CONFIG_H
#define OTA_CONFIG_H

/* Defaults are sufficient to compile the Agent; add overrides below. */

#endif /* OTA_CONFIG_H */
