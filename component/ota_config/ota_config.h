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
#if defined(BL602) || defined(BL702)
/* bl602: 1 KB blocks. Post-MQTT-connect free heap is only ~7 KB (the connected mbedTLS session's IN
 * buffer + the 5.7 KB socket RX buffer persist from the handshake), which must still fit the 6 KB OTA
 * agent task stack + its decode/event data buffers + the closeFile crypto working set. 1 KB blocks
 * (vs 2 KB) halve those buffers (decode 1 KB, event ~2.5 KB, MQTT net 1.5 KB) -> ~3 KB more heap, at
 * the cost of 2x the block count (slower download, fine for a demo). */
#define otaconfigLOG2_FILE_BLOCK_SIZE      ( 10UL )
#elif defined(BL616) || defined(BL616CL)
/* bl616: 1 KB, NOT 2 KB. Same trap as AmebaD's 4 KB note below: this account's AWS MQTT
 * file-delivery silently serves ZERO data/cbor blocks for any non-1KB get/cbor request
 * (CloudWatch shows Publish-In Success on get, no Publish-Out, no error). HW-verified on
 * bl616: 2 KB requests -> WaitingForFileBlock forever; 1 KB downloads. Heap fits: the
 * AWSOTA image uses picotls (not the mbedTLS IN-buffer layout the 2 KB choice was sized
 * against). */
#define otaconfigLOG2_FILE_BLOCK_SIZE      ( 10UL )
#else
/* AmebaD: 1 KB over the MQTT data plane, matching bl602. 4 KB (the old HTTPS-era value) is inside AWS's
 * documented 256 B..128 KB GetStream range, but AWS's MQTT file-delivery served ZERO data/cbor blocks
 * for a 4 KB get/cbor request from this account's stream (request accepted, no data, no
 * BlockSizeOutOfBounds error) while bl602's 1 KB requests download fine (2052 blocks served). */
#define otaconfigLOG2_FILE_BLOCK_SIZE      ( 10UL )
#endif

/* Number of blocks requested per data-plane request. Keep small: the PAL streams blocks to
 * ota_flash_feed (a sequential consumer), and a modest in-flight window keeps the RAM use bounded. */
#define otaconfigMAX_NUM_BLOCKS_REQUEST    ( 1U )

/* PAL verify strategy (ota_pal.c) -- two mutually exclusive paths, selected per platform:
 *
 *   Bouffalo (=0): STREAMING. Blocks are fed in arrival order to the sequential slot writer
 *     (ota_flash_feed / ota_flash_bl602) while the code-sign SHA-256 is hashed incrementally, so
 *     blocks MUST arrive in order (a reconnect-induced reorder fails the image). bl602's boot2+xz
 *     download has offset semantics that only make sense in order, and its OTA heap is tight (~7 KB
 *     free mid-download), so in-order MQTT download stays the proven path.
 *
 *   AmebaD (=1): RANDOM-WRITE + READ-BACK. Blocks are programmed at their offset in ANY order
 *     (ota_flash_write_at into a pre-erased slot); closeFile then reads the whole slot back and
 *     computes the 32-bit checksum + the ECDSA verify hash from the read-back bytes. Out-of-order is
 *     therefore free (no reorder buffering) and a WiFi-reconnect resume can no longer fail the
 *     download. REQUIRES RSIP=OFF: read-back == plaintext == the signed image (the bench/AWS default,
 *     project/amebad/Makefile RSIP?=OFF). On an RSIP=ON board the inactive-slot read-back is
 *     wrong-keystream garbage, so this path must not be used there. */
#if defined(BL602) || defined(BL702) || defined(BL616) || defined(BL616CL)
#define OTA_PAL_READBACK_VERIFY            ( 0 )   /* Bouffalo: streaming in-order */
#else
#define OTA_PAL_READBACK_VERIFY            ( 1 )   /* AmebaD: random-write + read-back (RSIP=OFF) */
#endif

/* ---- thing name ----
 * The Thing name = BLE MAC string (e.g. "AABBCCDDEEFF" / "AA:BB:CC:DD:EE:FF"), well under 64. */
#define otaconfigMAX_THINGNAME_LEN         ( 64U )

/* ---- agent task ----
 * OTA_EventProcessingTask stack (in words) + priority. The agent's heaviest stack user is
 * the closeFile path (mbedTLS x509 parse + ECDSA verify), so size generously. Priority sits
 * just under the telemetry/agent tasks. */
#if defined(BL602) || defined(BL702)
/* bl602: post-MQTT-connect free heap is only ~7 KB (the connected mbedTLS session's 6 KB IN buffer +
 * the 5.7 KB socket RX buffer both persist from the handshake, plus the OTA data buffers + s_net_buffer
 * consume the rest), so the 10 KB (2560-word) agent stack can't be created. The closeFile crypto
 * (x509 parse + ECDSA-P256 verify) runs on the HW ECP engine (small stack, no software modexp), so a
 * 6 KB stack suffices; configCHECK_FOR_STACK_OVERFLOW=2 catches it cleanly if it ever proves too tight. */
#define otaconfigSTACK_SIZE                ( 1536U )
#else
#define otaconfigSTACK_SIZE                ( 2560U )
#endif
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
