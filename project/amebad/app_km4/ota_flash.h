/*
 * ota_flash.h - AmebaD (RTL8721D) runtime OTA slot mechanics for the `ota_http` command.
 *
 * Streams an OTA_All.bin (the Realtek-format image built by image_tool/build_image_all.py: a
 * 32-byte OTA header + the image2[+IMG3] payload) into the INACTIVE OTA slot, then toggles the
 * active slot and reboots. NS-safe (TF-M): determines the running slot by reading OTA2's boot
 * signature via the flash_api (not the secure RSIP MMU), and writes the new slot's signature
 * LAST (power-down safe), mirroring the SDK's rtl8721d_ota.c download_new_fw_from_server_http.
 */
#ifndef OTA_FLASH_H
#define OTA_FLASH_H

#include <stdint.h>

/* Streaming OTA writer context. ota_flash_begin() fills the slot fields; ota_flash_feed() is then
 * called repeatedly with the HTTP response body bytes (the 32-byte header first, then the payload);
 * ota_flash_finish() verifies the checksum and arms the new slot. */
typedef struct
{
    uint32_t write_off;     /* flash offset of the inactive slot (where the new image goes)   */
    uint32_t active_off;    /* flash offset of the active slot (its signature gets cleared)   */
    uint8_t  hdr[ 32 ];     /* the 32-byte OTA header, accumulated from the first body bytes  */
    uint32_t hdr_got;       /* header bytes accumulated so far                                */
    int      have_hdr;      /* header parsed + slot erased                                    */
    uint32_t image_len;     /* payload length, from the header's IMAGE_LEN                    */
    uint32_t hdr_checksum;  /* expected checksum, from the header's CHECKSUM                  */
    uint32_t written;       /* payload bytes consumed so far                                  */
    uint32_t erased_to;     /* flash erased (incrementally) up to this offset                 */
    uint32_t checksum;      /* running 32-bit sum of all payload bytes                        */
    uint8_t  sig[ 8 ];      /* first 8 payload bytes (the boot signature), written LAST       */
} ota_ctx_t;

/* Detect the active/inactive slots; returns 0 on success, <0 on error. */
int ota_flash_begin( ota_ctx_t * c );

/* Feed `n` HTTP-body bytes (header then payload). Parses the header (erasing the slot once it is
 * complete), writes payload to flash skipping the signature, accumulates the checksum. Returns 0 on
 * success, <0 on a flash/validation error. */
int ota_flash_feed( ota_ctx_t * c, const uint8_t * d, uint32_t n );

/* After the whole body was fed: verify length + checksum, write the new slot's signature, clear the
 * old slot's signature. Returns 0 on success (caller should then ota_flash_reboot()), <0 on error. */
int ota_flash_finish( ota_ctx_t * c );

/* Full system reset (WDG) so the bootloader re-selects the now-active slot. Does not return. */
void ota_flash_reboot( void );

/* ---- OTA mutual exclusion -------------------------------------------------------------------- *
 * BLE (mcumgr), AWS IoT (ota_pal), and the ota_http console command all write the SAME inactive
 * dual-bank slot; running two at once corrupts the image. Each source claims this lock before
 * ota_flash_begin and releases it after finish/abort. A BLE claim self-expires a few seconds after
 * the last chunk so an abandoned (phone-disconnected) upload doesn't wedge the lock against a later
 * AWS job. */
typedef enum
{
    OTA_SRC_NONE = 0,
    OTA_SRC_BLE,        /* mcumgr SMP image upload   */
    OTA_SRC_AWS,        /* AWS IoT OTA agent (ota_pal) */
    OTA_SRC_HTTP        /* ota_http console command  */
} ota_src_t;

/* Claim (or refresh, if already held by `src`) the OTA lock. Returns 0 on success, -1 if another
 * source holds it. */
int       ota_lock_try( ota_src_t src );
/* Release the lock iff `src` currently holds it. */
void      ota_lock_release( ota_src_t src );
/* Current holder (OTA_SRC_NONE if free); expires a stale BLE claim first. */
ota_src_t ota_lock_holder( void );

#endif /* OTA_FLASH_H */
