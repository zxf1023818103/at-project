/*
 * ota_config.h — at-project configuration for the AWS IoT OTA Agent (component ota_config).
 *
 * Active because ota_config does NOT define OTA_DO_NOT_USE_CUSTOM_CONFIG; anything not
 * overridden here falls back to ota_config_defaults.h. The OTA Agent is wired on AmebaD KM4
 * (make AWS=ON) over the shared coreMQTT-Agent connection — see project/amebad/app_km4/
 * ota_app.c / ota_pal.c.
 */
#ifndef OTA_CONFIG_H
#define OTA_CONFIG_H

#include <stdio.h>

/* ---- control plane: MQTT (jobs) ----
 * The job control plane (notify-next / $next/get / UpdateJobExecution) is MQTT-only. */
#define configENABLED_CONTROL_PROTOCOL     ( OTA_CONTROL_OVER_MQTT )

/* ---- data plane: MQTT + HTTP, HTTP preferred ----
 * Both data protocols are compiled in (ota_app.c provides both the MQTT and HTTP interfaces).
 * setDataInterface() (ota_interface.c) then picks per the job document's "protocols" array:
 * a job created with --protocols HTTP offers only "HTTP", so the library uses the HTTP data
 * plane (presigned-S3 download over HTTPS) instead of the MQTT/CBOR stream. The firmware
 * DOWNLOAD therefore rides HTTPS; MQTT stays available as a fallback if a job offers only MQTT.
 * Primary = HTTP so HTTP wins when a job lists both. */
#define configENABLED_DATA_PROTOCOLS       ( OTA_DATA_OVER_MQTT | OTA_DATA_OVER_HTTP )
#define configOTA_PRIMARY_DATA_PROTOCOL    ( OTA_DATA_OVER_MQTT )

/* ---- block size ----
 * 2^12 = 4096-byte file blocks. MEASURED to be the heap-bound max on this target: bigger blocks grow
 * the per-block buffers (ota_pal s_decodeMem, the OtaEventData_t block buffer, ota_app's HTTP response
 * buffer), and the resulting lower idle heap can't absorb the S3 picotls handshake PEAK (cert-verify
 * working set + the contiguous rxReserve alloc) running concurrently with the AWS MQTT TLS:
 *   - 4 KB  (LOG2=12): idle heap ~80 KB -> OTA download OK.
 *   - 8 KB  (LOG2=13): idle heap ~69 KB -> 2nd (S3) TLS malloc FAILED.
 *   - 16 KB (LOG2=14): idle heap ~44.5 KB -> 2nd (S3) TLS malloc FAILED.
 * So the keep-alive-drop reconnect count stays at ~273 blocks; the speed win comes from the ota_app
 * in-call reconnect-retry (OTA_HTTP_REQ_RETRIES) instead, not from fewer blocks. Pairs with picotls. */
/* bl602/bl616 (AWSOTA=ON, MQTT data plane) use 2 KB blocks, not 4 KB: the ~26 KB free heap can't
 * hold the 4 KB block's decode/net/event .bss AND leave a contiguous ~6 KB block for the mbedTLS TLS
 * IN buffer (mbedtls_ssl_setup OOM'd at handshake). 2 KB blocks free ~6 KB of .bss. AmebaD keeps 4 KB. */
#if defined(BL602) || defined(BL616) || defined(BL616CL)
#define otaconfigLOG2_FILE_BLOCK_SIZE      ( 11UL )
#else
#define otaconfigLOG2_FILE_BLOCK_SIZE      ( 12UL )
#endif

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
