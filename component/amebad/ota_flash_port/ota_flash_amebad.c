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
 *
 * Write path = sector-buffered (erase + program per 4 KB sector), KM0 spin-parked for the burst.
 * Integrity is guaranteed end-to-end by the 32-bit checksum in the OTA header, computed over the
 * received bytes; a transmission error fails the upload before the slot is armed.
 *
 * NOTE - the STREAMING path (ota_flash_feed/finish, used by ota_http + BLE mcumgr) does NO read-back
 * verify: on an RSIP-fused board the image is stored encrypted and flash_stream_read returns the
 * RSIP-OTF-*decrypted* bytes, while our source buffer holds the *encrypted* image, so a byte-compare
 * read-back always mismatches even on a perfect write (and NS can't disable RSIP OTF to read raw). The
 * bootloader validates the decrypted image. The boot signature (first 8 payload bytes) is held back and
 * written last so a power loss never leaves a valid-looking-but-incomplete slot.
 *
 * The RANDOM-ACCESS path (ota_flash_*_random, used by the AWS OTA agent) programs blocks at their
 * offset in ANY order into a lazily-erased slot, then reads the whole payload back to verify the
 * checksum (and lets the caller re-hash it for the code-sign verify). This DOES read back and so
 * REQUIRES RSIP=OFF (read-back == the plaintext we wrote == the signed image); it must not be used on
 * an RSIP=ON board, where the inactive-slot read-back is wrong-keystream garbage (RSIP is address-keyed).
 * That is the bench/AWS default (project/amebad/Makefile RSIP?=OFF).
 */
#include "ota_flash.h"
#include <string.h>
#include "at_log.h"
#include "FreeRTOS.h"
#include "task.h"

/* ---- OTA mutual exclusion: shared ota_lock.c (ota_flash_config) ------------------------------ */
static void ota_flash_close_session( void );   /* relock the fw region; defined below (with the externs) */

/* ota_lock_release() hook: close a half-open write session (relock). Idempotent. */
void ota_flash_on_lock_released( void )
{
    ota_flash_close_session();
}

/* flash_api.c (component/amebad/mbed, already compiled). flash_t is opaque; the API ignores it. */
typedef struct flash_s flash_t;
extern int  flash_stream_write( flash_t * obj, uint32_t address, uint32_t len, uint8_t * data );
extern int  flash_stream_read( flash_t * obj, uint32_t address, uint32_t len, uint8_t * data );
extern void flash_erase_sector( flash_t * obj, uint32_t address );
/* sys_api.c (added to the mbed build): WDG full-system reset. */
extern void sys_reset( void );

/* rtl8721d_flash_ram.c: when nonzero, FLASH_Write_Lock/Unlock park KM0 by SPINNING it (IRQ-masked,
 * SRAM) instead of the wfe-sleep that NimBLE's KM0 IRQs wake mid-program -> XIP-fetch -> the slot
 * write corrupts. We set it for the whole OTA write burst. */
extern volatile unsigned int g_flash_spin_park;

/* Firmware-region write-protect (SR BP/CMP): the cross-platform flash lock (component/flash_cli_config,
 * board_flash_lock.h; AmebaD impl in monitor_port). The device write-protects the bottom 3 MB at boot,
 * and BOTH OTA slots sit inside that region -- so the write burst MUST unlock first and relock after.
 * WITHOUT this the erase/program silently no-op on a BP-locked sector (there is no read-back verify
 * here, see the file header) and we would arm an unwritten slot -> brick. */
#include "board_flash_lock.h"

/* The firmware region is unlocked + KM0 spin-parked between ota_flash_begin() and the relock. This
 * closes that window (relock + restore the wfe-sleep park); idempotent, so ota_flash_finish() (success
 * AND every error return) and every caller abort path (via ota_lock_release) can all call it safely. */
static volatile int s_write_session;

static void ota_flash_close_session( void )
{
    if( s_write_session )
    {
        s_write_session = 0;
        board_flash_lock_fw();     /* relock while still spin-parked (this SR write is itself a flash op) */
        g_flash_spin_park = 0;     /* then restore the normal wfe-sleep flash park */
    }
}

#define OTA1_OFF        0x006000u
#define OTA2_OFF        0x183000u
#define SLOT_SIZE       0x17D000u                 /* 1524 KB per slot */
#define SECTOR_SIZE     0x1000u                   /* 4 KB erase granularity (slot bases are 4K-aligned) */
#define OTA_HDR_LEN     32u
#define OTA_SIG         "81958711"                /* image2 boot signature (first 8 payload bytes)   */
#define OTA_FILE_SIGN   "OTA1"                    /* header OTA_SIGN field @ +8 (prepend_ota_header)  */

/* flash_api ignores the obj handle ((void)obj), so a NULL is fine - and flash_t is opaque here. */
static flash_t * const s_flash = NULL;

/* Sector accumulation buffer. Static: one OTA runs at a time. */
static uint8_t s_secbuf[ SECTOR_SIZE ];

/* Random-access path: which slot sectors have already been erased this OTA. On-demand (a sector is
 * erased the first time ota_flash_write_at touches it) so the erase cost overlaps the download instead
 * of a ~16 s up-front full-slot erase; the bitmap stops a later out-of-order block in the same sector
 * from re-erasing (which would wipe earlier blocks). One OTA at a time, so file-scope is fine. */
#define SLOT_SECTORS    ( SLOT_SIZE / SECTOR_SIZE )               /* 381 */
static uint8_t s_erased_bm[ ( SLOT_SECTORS + 7 ) / 8 ];          /* 48 bytes */

static int  bm_test( const uint8_t * bm, uint32_t i ) { return ( bm[ i >> 3 ] >> ( i & 7u ) ) & 1u; }
static void bm_set ( uint8_t * bm, uint32_t i ) { bm[ i >> 3 ] |= ( uint8_t ) ( 1u << ( i & 7u ) ); }

static uint32_t rd_le32( const uint8_t * p )
{
    return (uint32_t) p[ 0 ] | ( (uint32_t) p[ 1 ] << 8 ) |
           ( (uint32_t) p[ 2 ] << 16 ) | ( (uint32_t) p[ 3 ] << 24 );
}

/* Erase the sector at sec_base and program s_secbuf[lo,hi). The bytes below `lo` are left erased
 * (used to hold back the 8-byte boot signature in the first sector). */
static int flush_sector( uint32_t sec_base, uint32_t lo, uint32_t hi )
{
    flash_erase_sector( s_flash, sec_base );
    if( hi > lo && flash_stream_write( s_flash, sec_base + lo, hi - lo, s_secbuf + lo ) < 0 )
    {
        log_error( "[OTA] sector @0x%06x program failed", (unsigned) sec_base );
        return -1;
    }
    return 0;
}

int ota_flash_begin( ota_ctx_t * c )
{
    uint8_t sig[ 8 ];

    memset( c, 0, sizeof( *c ) );

    /* The bootloader boots OTA2 when its first 8 bytes are the valid boot signature, else OTA1. So
     * a valid OTA2 signature means OTA2 is the running slot -> write the new image to OTA1, and vice
     * versa. Read via the flash_api (user mode), which is NS-safe (no RSIP MMU access); on an RSIP
     * board flash_stream_read returns the decrypted bytes, so the stored sig reads back as the
     * plaintext OTA_SIG - which is exactly what we compare against. */
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
    g_flash_spin_park = 1;        /* spin-park KM0 for every flash op until ota_flash_finish */
    board_flash_unlock();         /* the OTA slots are in the write-protected bottom-3MB region */
    s_write_session = 1;          /* -> ota_flash_close_session() relocks on finish/abort */
    log_info( "[OTA] running slot @0x%06x, writing new image to @0x%06x (fw-region unlocked, KM0 spin-park ON)",
              (unsigned) c->active_off, (unsigned) c->write_off );
    return 0;
}

/* Flush the sector that payload offset [0..image_len) byte `upto_excl` has just completed (i.e. the
 * sector containing byte upto_excl-1). Called when crossing a 4 KB boundary and at finish. */
static int flush_pending_sector( ota_ctx_t * c, uint32_t sec_payload_base, uint32_t valid_len )
{
    uint32_t sec_base = c->write_off + sec_payload_base;
    /* In the first sector hold back the 8-byte boot signature (write it last in finish). */
    uint32_t lo = ( sec_payload_base == 0 ) ? 8u : 0u;

    if( valid_len <= lo )
        return 0;                       /* nothing to write yet (e.g. only sig bytes so far) */
    return flush_sector( sec_base, lo, valid_len );
}

/* Validate + parse the completed 32-byte OTA header into c (signature / checksum / image_len).
 * `how` tags the log line with the write path. Returns 0 on success, -1 on a bad header. */
static int ota_hdr_parse( ota_ctx_t * c, const char * how )
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
    log_info( "[OTA] image %u bytes, checksum 0x%08x -> %s",
              (unsigned) c->image_len, (unsigned) c->hdr_checksum, how );
    return 0;
}

int ota_flash_feed( ota_ctx_t * c, const uint8_t * d, uint32_t n )
{
    /* Phase 1: accumulate + validate the 32-byte OTA header. */
    while( c->hdr_got < OTA_HDR_LEN && n > 0 )
    {
        c->hdr[ c->hdr_got++ ] = *d++;
        n--;
        if( c->hdr_got == OTA_HDR_LEN && ota_hdr_parse( c, "sector write" ) != 0 )
            return -1;
    }
    if( !c->have_hdr )
        return 0;   /* still in the header */

    /* Phase 2: payload. Checksum covers ALL payload bytes (signature included). The first 8 bytes
     * are the boot signature (held in c->sig, written last). Bytes are accumulated into the current
     * 4 KB sector buffer and flushed (erase+program) on each sector boundary. */
    while( n > 0 )
    {
        uint32_t poff = c->written;
        uint32_t slot = poff & ( SECTOR_SIZE - 1 );        /* position within the current sector */
        uint32_t room = SECTOR_SIZE - slot;
        uint32_t take = n;
        uint32_t i;

        if( poff >= c->image_len )
            break;                                          /* ignore bytes past the declared image */
        if( take > c->image_len - poff )
            take = c->image_len - poff;
        if( take > room )
            take = room;                                    /* don't cross a sector boundary in one go */
        if( take == 0 )
            break;

        for( i = 0; i < take; i++ )
            c->checksum += d[ i ];
        for( i = poff; i < poff + take && i < 8; i++ )
            c->sig[ i ] = d[ i - poff ];

        memcpy( s_secbuf + slot, d, take );

        c->written += take;
        d          += take;
        n          -= take;

        /* Completed this sector? (filled to the 4 KB boundary, and more data/image remains) */
        if( ( ( c->written & ( SECTOR_SIZE - 1 ) ) == 0 ) )
        {
            uint32_t sec_payload_base = ( c->written - SECTOR_SIZE );
            if( flush_pending_sector( c, sec_payload_base, SECTOR_SIZE ) < 0 )
            {
                ota_flash_close_session();   /* relock the fw region before bailing */
                return -1;
            }
        }
    }
    return 0;
}

int ota_flash_finish( ota_ctx_t * c )
{
    uint8_t empty[ 8 ] = { 0 };

    if( !c->have_hdr || c->written != c->image_len )
    {
        log_error( "[OTA] short image: got %u of %u bytes", (unsigned) c->written, (unsigned) c->image_len );
        ota_flash_close_session();
        return -1;
    }
    if( c->checksum != c->hdr_checksum )
    {
        log_error( "[OTA] checksum mismatch: calc 0x%08x, header 0x%08x",
                   (unsigned) c->checksum, (unsigned) c->hdr_checksum );
        ota_flash_close_session();
        return -1;
    }

    /* Flush the final partial sector (if image_len isn't 4 KB-aligned). */
    if( ( c->written & ( SECTOR_SIZE - 1 ) ) != 0 )
    {
        uint32_t sec_payload_base = c->written & ~( SECTOR_SIZE - 1u );
        uint32_t valid_len        = c->written & ( SECTOR_SIZE - 1u );
        if( flush_pending_sector( c, sec_payload_base, valid_len ) < 0 )
        {
            ota_flash_close_session();
            return -1;
        }
    }

    /* Arm the new slot LAST: write its 8-byte boot signature into the (still-erased) first 8 bytes
     * of the first sector. The slot only becomes bootable now, so a power loss mid-download never
     * yields a half-written-but-valid slot. */
    if( flash_stream_write( s_flash, c->write_off, 8, c->sig ) < 0 )
    {
        log_error( "[OTA] boot-sig write failed; slot NOT armed" );
        ota_flash_close_session();
        return -1;
    }
    /* No read-back verify here: RSIP is address-keyed, so flash_stream_read of this (inactive-slot)
     * offset decrypts with the wrong keystream - it returns neither the raw ciphertext nor the true
     * plaintext, so any byte/sig compare gives a false failure even on a perfect write (only the
     * bootloader, using the correct XIP-address RSIP, can read it back). The integrity guarantee is
     * the OTA-header checksum verified above over the received bytes; the bootloader is the final
     * validator of the decrypted image. */
    /* Invalidate the old slot's signature so the bootloader deterministically picks the new one
     * (it prefers OTA2; clearing the old slot covers both directions). The running image is already
     * in XIP/cache, so clearing its on-flash signature does not disturb execution. */
    if( flash_stream_write( s_flash, c->active_off, 8, empty ) < 0 )
    {
        log_error( "[OTA] clearing old-slot signature failed" );
        ota_flash_close_session();
        return -1;
    }
    ota_flash_close_session();   /* writes done -> relock the fw region + restore the wfe-sleep park */
    log_info( "[OTA] image complete (checksum OK) + armed slot @0x%06x; clearing old @0x%06x",
              (unsigned) c->write_off, (unsigned) c->active_off );
    return 0;
}

/* ==== random-access variant (out-of-order blocks + read-back verify; RSIP=OFF only) =========== */

int ota_flash_begin_random( ota_ctx_t * c )
{
    int rc = ota_flash_begin( c );   /* slot detect + fw-region unlock + KM0 spin-park + open session */

    if( rc != 0 )
        return rc;
    memset( s_erased_bm, 0, sizeof( s_erased_bm ) );   /* nothing erased yet; write_at erases on demand */
    log_info( "[OTA] random-access write into slot @0x%06x (lazy per-sector erase)",
              (unsigned) c->write_off );
    return 0;
}

/* Program n bytes at absolute flash addr, erasing (once, bitmap-tracked) each 4 KB sector first. */
static int ota_random_program( ota_ctx_t * c, uint32_t addr, const uint8_t * d, uint32_t n )
{
    uint32_t a   = addr & ~( SECTOR_SIZE - 1u );
    uint32_t end = addr + n;

    for( ; a < end; a += SECTOR_SIZE )
    {
        uint32_t idx = ( a - c->write_off ) / SECTOR_SIZE;

        if( idx < SLOT_SECTORS && !bm_test( s_erased_bm, idx ) )
        {
            flash_erase_sector( s_flash, a );
            bm_set( s_erased_bm, idx );
        }
    }
    if( flash_stream_write( s_flash, addr, n, ( uint8_t * ) d ) < 0 )
    {
        log_error( "[OTA] random program @0x%06x len %u failed", (unsigned) addr, (unsigned) n );
        return -1;
    }
    return 0;
}

int ota_flash_write_at( ota_ctx_t * c, uint32_t off, const uint8_t * d, uint32_t len )
{
    /* Region 1: header bytes [0,32). A 1 KB block-0 delivers the whole 32-byte header in one call, but
     * track a high-water count so a (hypothetical) split is still parsed once complete. */
    if( off < OTA_HDR_LEN )
    {
        uint32_t hn = OTA_HDR_LEN - off;

        if( hn > len )
            hn = len;
        memcpy( c->hdr + off, d, hn );
        if( off + hn > c->hdr_got )
            c->hdr_got = off + hn;
        off += hn; d += hn; len -= hn;

        if( !c->have_hdr && c->hdr_got >= OTA_HDR_LEN &&
            ota_hdr_parse( c, "random write" ) != 0 )
            return -1;
        if( len == 0 )
            return 0;
    }

    /* off >= 32 now: payload, at payload offset p = off - 32. */
    {
        uint32_t p = off - OTA_HDR_LEN;

        /* Region 2: boot signature = payload [0,8). Held in c->sig, NOT programmed (written last by
         * ota_flash_finish_random so a power loss never arms a half-written slot). */
        if( p < 8u )
        {
            uint32_t sn = 8u - p;

            if( sn > len )
                sn = len;
            memcpy( c->sig + p, d, sn );
            p += sn; d += sn; len -= sn;
        }

        /* Region 3: payload [8, image_len) -> flash. */
        if( len > 0 )
        {
            if( c->have_hdr && ( p + len > c->image_len ) )       /* ignore bytes past the declared image */
                len = ( p < c->image_len ) ? ( c->image_len - p ) : 0u;
            if( len > 0 && ota_random_program( c, c->write_off + p, d, len ) < 0 )
                return -1;
        }
    }
    return 0;
}

int ota_flash_read_image( const ota_ctx_t * c, uint32_t off, uint8_t * out, uint32_t len )
{
    /* Region 1: header from the captured copy. */
    if( off < OTA_HDR_LEN )
    {
        uint32_t hn = OTA_HDR_LEN - off;

        if( hn > len )
            hn = len;
        memcpy( out, c->hdr + off, hn );
        off += hn; out += hn; len -= hn;
    }
    if( len == 0 )
        return 0;

    {
        uint32_t p = off - OTA_HDR_LEN;

        /* Region 2: boot sig -> the held copy (flash there is still erased until finish arms it). */
        if( p < 8u )
        {
            uint32_t sn = 8u - p;

            if( sn > len )
                sn = len;
            memcpy( out, c->sig + p, sn );
            p += sn; out += sn; len -= sn;
        }
        /* Region 3: payload read-back from flash (RSIP-OFF plaintext). */
        if( len > 0 && flash_stream_read( s_flash, c->write_off + p, len, out ) < 0 )
        {
            log_error( "[OTA] read-back @0x%06x len %u failed", (unsigned) ( c->write_off + p ), (unsigned) len );
            return -1;
        }
    }
    return 0;
}

int ota_flash_finish_random( ota_ctx_t * c )
{
    uint8_t  buf[ 256 ];
    uint8_t  empty[ 8 ] = { 0 };
    uint32_t p, sum = 0;

    if( !c->have_hdr )
    {
        log_error( "[OTA] finish: header never arrived" );
        ota_flash_close_session();
        return -1;
    }

    /* Read the whole payload back (RSIP-OFF plaintext) and recompute the 32-bit checksum, splicing the
     * held boot sig over payload [0,8) (still erased on flash). A missing block reads back as 0xFF, so
     * the checksum catches an incomplete download. Matches ota_flash_feed's sum-of-all-payload-bytes. */
    for( p = 0; p < c->image_len; )
    {
        uint32_t n = c->image_len - p;
        uint32_t i;

        if( n > sizeof( buf ) )
            n = sizeof( buf );
        if( flash_stream_read( s_flash, c->write_off + p, n, buf ) < 0 )
        {
            log_error( "[OTA] finish: read-back @0x%06x failed", (unsigned) ( c->write_off + p ) );
            ota_flash_close_session();
            return -1;
        }
        for( i = 0; i < n; i++ )
            sum += ( p + i < 8u ) ? c->sig[ p + i ] : buf[ i ];
        p += n;
    }
    c->checksum = sum;
    c->written  = c->image_len;   /* parity with the streaming path's bookkeeping */

    if( sum != c->hdr_checksum )
    {
        log_error( "[OTA] checksum mismatch (read-back): calc 0x%08x, header 0x%08x",
                   (unsigned) sum, (unsigned) c->hdr_checksum );
        ota_flash_close_session();
        return -1;
    }

    /* Arm the new slot LAST: write the held boot signature into the still-erased first 8 bytes. */
    if( flash_stream_write( s_flash, c->write_off, 8, c->sig ) < 0 )
    {
        log_error( "[OTA] boot-sig write failed; slot NOT armed" );
        ota_flash_close_session();
        return -1;
    }
    if( flash_stream_write( s_flash, c->active_off, 8, empty ) < 0 )   /* invalidate the old slot */
    {
        log_error( "[OTA] clearing old-slot signature failed" );
        ota_flash_close_session();
        return -1;
    }
    ota_flash_close_session();
    log_info( "[OTA] image complete (read-back checksum OK) + armed slot @0x%06x; clearing old @0x%06x",
              (unsigned) c->write_off, (unsigned) c->active_off );
    return 0;
}

void ota_flash_reboot( void )
{
    log_info( "[OTA] rebooting into the new image..." );
    /* WDG full-system reset (SDK sys_reset). Takes the whole system (incl. Wi-Fi driver) down so the
     * reboot comes up clean; also flushes any pending env/LFS writes (50 ms WDG window). */
    sys_reset();
    for( ; ; ) {}
}
