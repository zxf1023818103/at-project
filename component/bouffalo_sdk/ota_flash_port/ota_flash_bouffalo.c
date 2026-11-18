/*
 * ota_flash_bouffalo.c - Bouffalo (BL602/BL616/BL616CL) impl of the shared OTA slot writer (component/ota_flash_config,
 * ota_flash.h), the streaming sink for `ota_http`. Approach B: drive bflb_flash + the pt_table
 * directly (the AmebaD ota_flash.c analog), not the SDK bflb_ota (which needs the size up front and
 * leaves a compressed image for boot2).
 *
 * XZ: boot2 auto-decompresses an on-flash FW_OTA.xz on first boot, which collides with our firmware
 * write-protect (the slot is locked, boot2 can't rewrite it) -> the image won't start. So we detect
 * the xz magic (FD 37 7A 58 5A 00) in the stream head and, if present, decompress inline (xz-embedded,
 * 32 KB dict - matching boot2) and write the RAW image; otherwise write the bytes as-is. Either way
 * boot2 never needs to write flash. See the reference_bl602_boot2_xz_writeprotect memory.
 *
 * Slot: pt_table's FW entry has two slots (start_address[active] / [!active]); we stream into the
 * inactive one, then swap active_index + age++ and let boot2 pick it. No hash in the pt entry (boot2
 * validates the FW image's own header), so integrity rides the image + the xz CRC.
 */
#include "ota_flash.h"
#include "board_flash_lock.h"

#include <string.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "partition.h"        /* pt_table_* + pt_table_stuff_config / pt_table_entry_config */
#include "bflb_flash.h"       /* bflb_flash_erase / write / read */
#include "xz.h"               /* xz-embedded streaming decoder */
#include "at_log.h"           /* rxi log */

extern int bl_sys_reset_por(void);   /* bflb_sys: full power-on reset (resolves at image link) */

#define FW_ERASE_BLOCK   ( 4u * 1024u )
#define XZ_DICT_MAX      ( 1u << 15 )        /* 32 KB - matches boot2's xz_dec_init */
static const uint8_t XZ_MAGIC[ 6 ] = { 0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00 };

/* ---- OTA mutual exclusion: shared ota_lock.c (ota_flash_config) ------------------------------ */
static void ota_flash_close_session( void );   /* relock + free xz; defined below */

/* ota_lock_release() hook: close a half-open write session (relock + free xz). Idempotent. */
void ota_flash_on_lock_released( void )
{
    ota_flash_close_session();
}

/* ---- slot writer state (one OTA at a time) --------------------------------------------------- */
static pt_table_stuff_config s_pt[ 2 ];
static pt_table_entry_config s_fw;
static pt_table_id_type      s_active_id;
static uint32_t s_ota_addr;      /* inactive slot base                        */
static uint32_t s_part_size;     /* inactive slot size                        */
static uint32_t s_write_off;     /* raw bytes written to the slot so far      */
static uint32_t s_erased_upto;   /* slot erased (block-aligned) up to here    */
static int      s_session;       /* 1 between begin() and relock              */
static int      s_is_xz;         /* -1 unknown, 0 raw, 1 xz                    */
static uint8_t  s_magic[ 6 ];
static unsigned s_magic_got;
static struct xz_dec * s_xz;
static int      s_xz_done;
static uint8_t  s_xzout[ 2048 ]; /* xz decompress output chunk                */

/* Erase 4 KB blocks lazily so a sequential write only erases what it reaches. */
static int ensure_erased( uint32_t upto )
{
    while( s_erased_upto < upto )
    {
        if( s_erased_upto >= s_part_size ) { return -1; }         /* would overflow the slot */
        if( bflb_flash_erase( s_ota_addr + s_erased_upto, FW_ERASE_BLOCK ) != 0 ) { return -1; }
        s_erased_upto += FW_ERASE_BLOCK;
    }
    return 0;
}

/* Write raw (already-decompressed) bytes to the inactive slot at the running offset. */
static int raw_write( const uint8_t * buf, uint32_t len )
{
    if( s_write_off + len > s_part_size ) { return -1; }
    if( ensure_erased( s_write_off + len ) != 0 ) { return -1; }
    if( bflb_flash_write( s_ota_addr + s_write_off, ( uint8_t * ) buf, len ) != 0 ) { return -1; }
    s_write_off += len;
    return 0;
}

/* Feed `inlen` compressed bytes to the xz decoder, writing each decompressed chunk to flash. */
static int xz_process( const uint8_t * in, uint32_t inlen )
{
    struct xz_buf b;
    enum xz_ret   r;

    b.in = in; b.in_pos = 0; b.in_size = inlen;
    do
    {
        b.out = s_xzout; b.out_pos = 0; b.out_size = sizeof( s_xzout );
        r = xz_dec_run( s_xz, &b );
        if( b.out_pos > 0 && raw_write( s_xzout, ( uint32_t ) b.out_pos ) != 0 ) { return -1; }
        if( r != XZ_OK && r != XZ_STREAM_END ) { return -1; }
        if( r == XZ_STREAM_END ) { s_xz_done = 1; return 0; }
    } while( b.in_pos < b.in_size || b.out_pos == b.out_size );
    return 0;
}

static int process_bytes( const uint8_t * buf, uint32_t len )
{
    return s_is_xz ? xz_process( buf, len ) : raw_write( buf, len );
}

int ota_flash_begin( ota_ctx_t * c )
{
    memset( c, 0, sizeof( *c ) );
    s_write_off = 0; s_erased_upto = 0; s_is_xz = -1; s_magic_got = 0; s_xz = NULL; s_xz_done = 0;

    pt_table_set_flash_operation( bflb_flash_erase, bflb_flash_write, bflb_flash_read );
    s_active_id = pt_table_get_active_partition_need_lock( s_pt );
    if( s_active_id == PT_TABLE_ID_INVALID )
    {
        log_error( "[OTA] no valid partition table" );
        return -1;
    }
    if( pt_table_get_active_entries_by_name( &s_pt[ s_active_id ], ( uint8_t * ) "FW", &s_fw ) != 0 )
    {
        log_error( "[OTA] no FW partition entry" );
        return -1;
    }
    s_ota_addr  = s_fw.start_address[ !s_fw.active_index ];   /* inactive slot */
    s_part_size = s_fw.max_len[ !s_fw.active_index ];

    board_flash_unlock();   /* the FW slots are in the write-protected bottom 3 MB */
    s_session = 1;
    log_info( "[OTA] active slot @0x%06x -> writing new image to @0x%06x (size<=%u)",
              ( unsigned ) s_fw.start_address[ s_fw.active_index ], ( unsigned ) s_ota_addr,
              ( unsigned ) s_part_size );
    return 0;
}

int ota_flash_feed( ota_ctx_t * c, const uint8_t * d, uint32_t n )
{
    /* Head: buffer the first 6 bytes to sniff the xz magic before deciding the path. */
    while( s_magic_got < sizeof( s_magic ) && n > 0 )
    {
        s_magic[ s_magic_got++ ] = *d++;
        n--;
    }
    if( s_magic_got < sizeof( s_magic ) )
    {
        return 0;   /* still collecting the head (tiny first chunk) */
    }
    if( s_is_xz < 0 )
    {
        s_is_xz = ( memcmp( s_magic, XZ_MAGIC, sizeof( s_magic ) ) == 0 ) ? 1 : 0;
        if( s_is_xz )
        {
            xz_crc32_init();
            /* XZ_DYNALLOC (not PREALLOC): allocate only the dict size the STREAM actually declares,
             * not the full 32 KB max - bl602's ~68 KB heap can't hold a 32 KB dict + LZMA2 state
             * during ota_http, so OTA .xz images must be compressed with a small dict (xz
             * --lzma2=dict=8KiB); boot2's 32 KB decoder still handles them. */
            s_xz = xz_dec_init( XZ_DYNALLOC, XZ_DICT_MAX );
            if( s_xz == NULL ) { log_error( "[OTA] xz_dec_init failed (heap)" ); return -1; }
            log_info( "[OTA] xz-compressed image -> decompressing inline" );
        }
        if( process_bytes( s_magic, sizeof( s_magic ) ) != 0 ) { return -1; }   /* the buffered head */
    }
    if( n > 0 && process_bytes( d, n ) != 0 ) { return -1; }
    c->have_hdr = 1;   /* signal ota_http that a valid stream is in progress */
    return 0;
}

int ota_flash_finish( ota_ctx_t * c )
{
    ( void ) c;
    if( s_is_xz == 1 && !s_xz_done )
    {
        ( void ) xz_process( ( const uint8_t * ) "", 0 );   /* flush trailing output */
        if( !s_xz_done )
        {
            log_error( "[OTA] xz stream truncated" );
            ota_flash_close_session();
            return -1;
        }
    }
    if( s_write_off == 0 )
    {
        log_error( "[OTA] empty image" );
        ota_flash_close_session();
        return -1;
    }

    /* Arm the new slot: record its length, flip active_index, bump age; boot2 picks the newer slot. */
    s_fw.len          = s_write_off;
    s_fw.active_index = !( s_fw.active_index & 0x01 );
    s_fw.age++;
    if( pt_table_update_entry( !s_active_id, &s_pt[ s_active_id ], &s_fw ) != 0 )
    {
        log_error( "[OTA] pt_table update failed" );
        ota_flash_close_session();
        return -1;
    }
    log_info( "[OTA] image complete (%u bytes) + armed inactive slot (age=%u)",
              ( unsigned ) s_write_off, ( unsigned ) s_fw.age );
    ota_flash_close_session();
    return 0;
}

static void ota_flash_close_session( void )
{
    if( !s_session ) { return; }
    s_session = 0;
    if( s_xz != NULL ) { xz_dec_end( s_xz ); s_xz = NULL; }
    board_flash_lock_fw();   /* relock the firmware region */
}

void ota_flash_reboot( void )
{
    log_info( "[OTA] rebooting into the new image..." );
    bl_sys_reset_por();
    for( ; ; ) { }
}
