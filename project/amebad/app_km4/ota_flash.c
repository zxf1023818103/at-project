/*
 * ota_flash.c - AmebaD (RTL8721D) runtime OTA slot mechanics (see ota_flash.h).
 *
 * Mirrors the proven logic of the SDK's misc/rtl8721d_ota.c (download_new_fw_from_server_http /
 * change_ota_signature / verify_ota_checksum) but is NS-safe for the TF-M build: it never touches
 * the secure RSIP MMU (ota_get_cur_index reads it and would SecureFault from NS). Instead the
 * running slot is inferred from OTA2's boot signature, read through the flash_api.
 *
 * Flash addresses here are SPI-flash OFFSETS (flash_api adds SPI_FLASH_BASE 0x08000000 internally):
 *   OTA1 @ 0x006000 .. 0x183000   (slot 0x17D000 = 1524 KB)
 *   OTA2 @ 0x183000 .. 0x300000   (LittleFS starts at 0x300000)
 */
#include "ota_flash.h"
#include <string.h>
#include "at_log.h"

/* flash_api.c (component/amebad/mbed, already compiled). flash_t is opaque; the API ignores it. */
typedef struct flash_s flash_t;
extern int  flash_stream_write( flash_t * obj, uint32_t address, uint32_t len, uint8_t * data );
extern int  flash_stream_read( flash_t * obj, uint32_t address, uint32_t len, uint8_t * data );
extern void flash_erase_sector( flash_t * obj, uint32_t address );
/* sys_api.c (added to the mbed build): WDG full-system reset. */
extern void sys_reset( void );

/* rtl8721d_flash_ram.c: when nonzero, FLASH_Write_Lock/Unlock park KM0 by SPINNING it (IRQ-masked,
 * SRAM) instead of the wfe-sleep that NimBLE's KM0 IRQs wake mid-program -> XIP-fetch -> the slot
 * write corrupts. We set it for the whole OTA write burst (confirmed fix for the IMG2-Invalid bug). */
extern volatile unsigned int g_flash_spin_park;

#define OTA1_OFF        0x006000u
#define OTA2_OFF        0x183000u
#define SLOT_SIZE       0x17D000u                 /* 1524 KB per slot */
#define SECTOR_SIZE     0x1000u                   /* 4 KB erase granularity (slot bases are 4K-aligned) */
#define OTA_HDR_LEN     32u
#define OTA_SIG         "81958711"                /* image2 boot signature (first 8 payload bytes)   */
#define OTA_FILE_SIGN   "OTA1"                    /* header OTA_SIGN field @ +8 (prepend_ota_header)  */

/* flash_api ignores the obj handle ((void)obj), so a NULL is fine - and flash_t is opaque here. */
static flash_t * const s_flash = NULL;

static uint32_t rd_le32( const uint8_t * p )
{
    return (uint32_t) p[ 0 ] | ( (uint32_t) p[ 1 ] << 8 ) |
           ( (uint32_t) p[ 2 ] << 16 ) | ( (uint32_t) p[ 3 ] << 24 );
}

int ota_flash_begin( ota_ctx_t * c )
{
    uint8_t sig[ 8 ];

    memset( c, 0, sizeof( *c ) );

    /* The bootloader boots OTA2 when its first 8 bytes are the valid boot signature, else OTA1. So
     * a valid OTA2 signature means OTA2 is the running slot -> write the new image to OTA1, and vice
     * versa. Read via the flash_api (user mode), which is NS-safe (no RSIP MMU access). */
    if( flash_stream_read( s_flash, OTA2_OFF, sizeof( sig ), sig ) < 0 )
    {
        log_error( "[OTA] flash read (slot detect) failed" );
        return -1;
    }
    if( memcmp( sig, OTA_SIG, 8 ) == 0 )
    {
        c->active_off = OTA2_OFF;   /* running OTA2 */
        c->write_off  = OTA1_OFF;
    }
    else
    {
        c->active_off = OTA1_OFF;   /* running OTA1 */
        c->write_off  = OTA2_OFF;
    }
    c->erased_to = c->write_off;
    g_flash_spin_park = 1;   /* spin-park KM0 (not wfe-sleep) for every flash op until ota_flash_finish */
    log_info( "[OTA] running slot @0x%06x, writing new image to @0x%06x (KM0 spin-park ON)",
              (unsigned) c->active_off, (unsigned) c->write_off );
    return 0;
}

/* Erase (incrementally) up to `upto`, so each 4 KB sector is cleared just before it is written -
 * interleaved with the network recv so a flash op never stalls WiFi for long. */
static void erase_upto( ota_ctx_t * c, uint32_t upto )
{
    if( upto > c->write_off + SLOT_SIZE )
        upto = c->write_off + SLOT_SIZE;
    while( c->erased_to < upto )
    {
        flash_erase_sector( s_flash, c->erased_to );
        c->erased_to += SECTOR_SIZE;
    }
}

int ota_flash_feed( ota_ctx_t * c, const uint8_t * d, uint32_t n )
{
    /* Phase 1: accumulate + validate the 32-byte OTA header. */
    while( c->hdr_got < OTA_HDR_LEN && n > 0 )
    {
        c->hdr[ c->hdr_got++ ] = *d++;
        n--;
        if( c->hdr_got == OTA_HDR_LEN )
        {
            if( memcmp( c->hdr + 8, OTA_FILE_SIGN, 4 ) != 0 )
            {
                log_error( "[OTA] bad OTA header signature (not '%s')", OTA_FILE_SIGN );
                return -1;
            }
            c->hdr_checksum = rd_le32( c->hdr + 16 );
            c->image_len    = rd_le32( c->hdr + 20 );
            if( c->image_len == 0 || c->image_len > SLOT_SIZE )
            {
                log_error( "[OTA] bad image length %u", (unsigned) c->image_len );
                return -1;
            }
            c->have_hdr = 1;
            log_info( "[OTA] image %u bytes, checksum 0x%08x -> erasing + writing",
                      (unsigned) c->image_len, (unsigned) c->hdr_checksum );
        }
    }
    if( !c->have_hdr )
        return 0;   /* still in the header */

    /* Phase 2: payload. First 8 bytes are the boot signature (held, written last); the rest is
     * streamed to flash. Checksum covers ALL payload bytes (signature included). */
    while( n > 0 )
    {
        uint32_t poff = c->written;
        uint32_t take = n;
        uint32_t i;

        if( poff + take > c->image_len )
            take = c->image_len - poff;     /* ignore anything past the declared image (e.g. trailers) */
        if( take == 0 )
            break;

        for( i = 0; i < take; i++ )
            c->checksum += d[ i ];

        /* capture the signature bytes that fall in [poff, poff+take) */
        for( i = poff; i < poff + take && i < 8; i++ )
            c->sig[ i ] = d[ i - poff ];

        /* write the bytes at offset >= 8 to flash (skip the signature) */
        if( poff + take > 8 )
        {
            uint32_t wstart = ( poff > 8 ) ? poff : 8;          /* payload offset to start writing */
            uint32_t wlen   = ( poff + take ) - wstart;
            const uint8_t * wsrc = d + ( wstart - poff );
            erase_upto( c, c->write_off + wstart + wlen );
            if( flash_stream_write( s_flash, c->write_off + wstart, wlen, (uint8_t *) wsrc ) < 0 )
            {
                log_error( "[OTA] flash write failed @0x%06x", (unsigned) ( c->write_off + wstart ) );
                return -1;
            }
        }

        c->written += take;
        d          += take;
        n          -= take;
    }
    return 0;
}

int ota_flash_finish( ota_ctx_t * c )
{
    uint8_t empty[ 8 ] = { 0 };

    if( !c->have_hdr || c->written != c->image_len )
    {
        log_error( "[OTA] short image: got %u of %u bytes", (unsigned) c->written, (unsigned) c->image_len );
        return -1;
    }
    if( c->checksum != c->hdr_checksum )
    {
        log_error( "[OTA] checksum mismatch: calc 0x%08x, header 0x%08x",
                   (unsigned) c->checksum, (unsigned) c->hdr_checksum );
        return -1;
    }

    /* Arm the new slot LAST: write its 8-byte boot signature (the slot only becomes bootable now,
     * so a power loss mid-download never yields a half-written-but-valid slot). */
    if( flash_stream_write( s_flash, c->write_off, 8, c->sig ) < 0 )
    {
        log_error( "[OTA] writing new-slot signature failed" );
        return -1;
    }
    /* Invalidate the old slot's signature so the bootloader deterministically picks the new one
     * (it prefers OTA2; clearing the old slot covers both directions). The running image is already
     * in XIP/cache, so clearing its on-flash signature does not disturb execution. */
    if( flash_stream_write( s_flash, c->active_off, 8, empty ) < 0 )
    {
        log_error( "[OTA] clearing old-slot signature failed" );
        return -1;
    }
    g_flash_spin_park = 0;   /* OTA writes done -> restore the normal wfe-sleep flash lock */
    log_info( "[OTA] image verified + armed slot @0x%06x; clearing old @0x%06x",
              (unsigned) c->write_off, (unsigned) c->active_off );
    return 0;
}

void ota_flash_reboot( void )
{
    log_info( "[OTA] rebooting into the new image..." );
    /* WDG full-system reset (SDK sys_reset: sets BKUP_REG0 |= BIT_KM4SYS_RESET_HAPPEN, then a 50 ms WDG).
     *
     * This was briefly switched to a Cortex-M33 SYSRESETREQ (SCB->AIRCR, + the ROM reset handshake at
     * 0x480003F8). Don't: SYSRESETREQ resets only the KM4 CPU core, NOT the Wi-Fi driver/MAC (lib_wlan
     * + the WL hardware block) - which is left running in a stale post-reset state and faults. sys_reset's
     * WDG reset takes the whole system down (Wi-Fi driver included), so the reboot comes up clean. (The
     * "warm-reset heap corruption" earlier blamed on stale SRAM was really this un-reset Wi-Fi driver.)
     * Bonus: the 50 ms WDG window also flushes the OTA_SELFTEST env/LFS write that ota_pal closeFile()
     * makes - SYSRESETREQ rebooted too fast for it to land, so getPlatformImageState() read Valid instead
     * of PendingCommit and the OTA self-test was rejected (OtaErrImageStateMismatch, job FAILED even
     * though the firmware updated); with sys_reset the flag survives and the self-test commits (SUCCEEDED). */
    sys_reset();
    for( ; ; ) {}
}
