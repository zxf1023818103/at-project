/*
 * ota_config.h — at-project configuration for the AWS IoT OTA Agent (component ota_config).
 *
 * Active because ota_config does NOT define OTA_DO_NOT_USE_CUSTOM_CONFIG; anything not
 * overridden here falls back to ota_config_defaults.h. The OTA Agent is wired on AmebaD KM4
 * (make AWS=ON) over the shared coreMQTT-Agent connection — see component/amebad/app_km4/
 * ota_app.c / ota_pal.c.
 */
#ifndef OTA_CONFIG_H
#define OTA_CONFIG_H

#include <stdio.h>

/* ---- data plane: MQTT only (no HTTP) ----
 * Control + data both over MQTT; the HTTP interface is left zeroed in ota_app.c. The
 * defaults (configENABLED_CONTROL_PROTOCOL / configENABLED_DATA_PROTOCOLS /
 * configOTA_PRIMARY_DATA_PROTOCOL) are already MQTT-only, set explicitly for clarity. */
#define configENABLED_CONTROL_PROTOCOL     ( OTA_CONTROL_OVER_MQTT )
#define configENABLED_DATA_PROTOCOLS       ( OTA_DATA_OVER_MQTT )
#define configOTA_PRIMARY_DATA_PROTOCOL    ( OTA_DATA_OVER_MQTT )

/* ---- block size ----
 * 2^12 = 4096-byte file blocks (matches the ota_pal decode buffer s_decodeMem). */
#define otaconfigLOG2_FILE_BLOCK_SIZE      ( 12UL )

/* Number of blocks requested per data-plane request. Keep small: the AmebaD PAL streams
 * blocks sequentially to ota_flash_feed (a sequential consumer), and a modest in-flight
 * window keeps the data plane ordered + the RAM use bounded. */
#define otaconfigMAX_NUM_BLOCKS_REQUEST    ( 1U )

/* ---- thing name ----
 * The Thing name = BLE MAC string (e.g. "AABBCCDDEEFF" / "AA:BB:CC:DD:EE:FF"), well under 64. */
#define otaconfigMAX_THINGNAME_LEN         ( 64U )

/* ---- agent task ----
 * OTA_EventProcessingTask stack (in words) + priority. The agent's heaviest stack user is
 * the closeFile path (mbedTLS x509 parse + ECDSA verify), so size generously. Priority sits
 * just under the telemetry/agent tasks. */
#define otaconfigSTACK_SIZE                ( 2560U )
#define otaconfigAGENT_PRIORITY            ( tskIDLE_PRIORITY + 2 )

/* ---- self-test ----
 * No real A/B self-test on this platform (see ota_pal.c / ota_app.c). The library still
 * arms a self-test response timer when an image reports pending-commit; keep the default
 * wait but it is effectively unused because getPlatformImageState() returns Valid. */

/* ---- logging: route to the console as [OTA] lines ----
 * The OTA library passes the format+args already parenthesised, so `printf message` works. */
#define LogError( message )    do { printf( "[OTA] ERR " ); printf message; printf( "\r\n" ); } while( 0 )
#define LogWarn( message )     do { printf( "[OTA] WRN " ); printf message; printf( "\r\n" ); } while( 0 )
#define LogInfo( message )     do { printf( "[OTA] " );     printf message; printf( "\r\n" ); } while( 0 )
#define LogDebug( message )

#endif /* OTA_CONFIG_H */
