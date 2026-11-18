/*
 * board_flash_bouffalo.c - Bouffalo (BL602/BL616/BL616CL) impl of the cross-platform firmware flash
 * write-protect (component/flash_cli_config, board_flash_lock.h) over the LHAL bflb_sflash. Same
 * W25Q/GD25 SR1/SR2 BP/CMP layout as AmebaD: LOCK_FW = SR1 0x14 + SR2 CMP=1 (bottom 3 MB locked, top
 * 1 MB LittleFS writable), LOCK_ALL = SR1 0x1C, UNLOCK = 0. SR1+SR2 are written together via the
 * 16-bit WRSR (0x01 + two data bytes: SR1 then SR2), read back with RDSR1 (0x05) / RDSR2 (0x35).
 *
 * XIP-quiesce (BL602 + BL616/CL): the manual SR SPI commands must run with the flash XIP/cache + IRQs
 * frozen, else a raw RDSR/WRSR races the XIP continuous-read -> the next XIP instruction fetch returns
 * garbage. On bl616 this is not academic: the un-quiesced version hung the boot in Clock_Peripheral_
 * Clock_Get and a raced WRSR left a corrupted (CMP-inverted) status register that bricked the flash for
 * reflashing. We follow the exact sequence bflb_flash.c uses (bflb_irq_save -> bflb_xip_sflash_opt_enter
 * -> state_save -> ...cmds... -> state_restore[_ext] -> opt_exit -> bflb_irq_restore) and run the whole
 * transaction from TCM (ATTR .tcm_code) so no instruction is fetched from flash during the window. Only
 * the restore form differs per chip: BL602 = state_restore_ext, BL616/CL = state_restore.
 */
#include "board_flash_lock.h"

#include <stdint.h>
#include <stdio.h>      /* printf — GCC 14 (bl618dg's Zephyr toolchain) rejects
                         * the implicit declaration that older GCCs allowed. */

/* Place the quiesced transaction in TCM (the linker collects *(.tcm_code.*): bl602_flash.ld on bl602,
 * the generated bl616 linker.ld's itcm section on bl616). noinline
 * is REQUIRED: a section attribute does not stop the compiler inlining this static function into its
 * XIP callers, which would run the XIP-off window from flash -> instruction-fetch hang. */
#define BF_TCM __attribute__( ( noinline, section( ".tcm_code.board_flash_bouffalo" ) ) )

/* LHAL flash APIs (drivers/lhal). The cfg is opaque here (pointer passed straight through). We use the
 * *_with_cmd read/write forms with EXPLICIT opcodes: on the bl602dk GD25Q32 (jedec 0x1640C8) the cfg's
 * read_reg_cmd[1] is not RDSR2 (0x35), so the index form can't reach SR2. */
struct spi_flash_cfg_type;
extern void bflb_flash_get_cfg( uint8_t ** cfg_addr, uint32_t * len );
extern int  bflb_sflash_write_enable( struct spi_flash_cfg_type * cfg );   /* WREN (0x06) before WRSR */
extern int  bflb_sflash_read_reg_with_cmd( struct spi_flash_cfg_type * cfg, uint8_t cmd,
                                           uint8_t * reg_value, uint8_t reg_len );
extern int  bflb_sflash_write_reg_with_cmd( struct spi_flash_cfg_type * cfg, uint8_t cmd,
                                            uint8_t * reg_value, uint8_t reg_len );

#if defined( BL602 ) || defined( BL616 ) || defined( BL616CL )
/* XIP/cache + IRQ freeze around the manual flash commands (bl602_std bl602_romapi.c /
 * bl602_xip_sflash_ext.c; bl616 lhal bflb_xip_sflash.c). The enter/save/opt entry points are
 * common; only the restore form differs: BL602 uses the _ext variant, BL616/CL the plain one
 * (mirrors bflb_flash.c's own #if split). */
extern uintptr_t bflb_irq_save( void );
extern void      bflb_irq_restore( uintptr_t flags );
extern void      bflb_xip_sflash_opt_enter( uint8_t * aes_enable );
extern void      bflb_xip_sflash_opt_exit( uint8_t aes_enable );
extern int       bflb_xip_sflash_state_save( struct spi_flash_cfg_type * cfg, uint32_t * offset,
                                             uint8_t group, uint8_t bank );
#if defined( BL602 )
extern int       bflb_xip_sflash_state_restore_ext( struct spi_flash_cfg_type * cfg, uint32_t offset,
                                                    uint8_t group, uint8_t bank );
#else
extern int       bflb_xip_sflash_state_restore( struct spi_flash_cfg_type * cfg, uint32_t offset,
                                                uint8_t group, uint8_t bank );
#endif
#endif

#define SF_CMD_RDSR1  0x05   /* Read Status Register-1  */
#define SF_CMD_RDSR2  0x35   /* Read Status Register-2 (holds CMP at bit 6) */
#define SF_CMD_WRSR   0x01   /* Write Status Register: GD25Q32 accepts the 16-bit form (SR1 then SR2) */

/* Flash models the SR1=0x14 + SR2.CMP=1 "bottom 3 MB locked, top 1 MB (LFS) writable" encoding
 * is VERIFIED on. The BP/CMP protection map is vendor/part-specific: the same SR bits on a
 * different part can cover the LFS region too — HW-caught on a second bl616 board, where the
 * boot-time lock left LittleFS unwritable (every prog failed read-back -> cascade of
 * "Bad block" -> "No more free space"), silently eating the OTA self-test flag and failing
 * the job with 0x11 ImageStateMismatch. Unknown JEDEC ids now FAIL OPEN (no write-protect). */
static const uint32_t k_lock_jedec_ok[] =
{
    0x1640C8,   /* GD25Q32  (bl602dk)  */
    0x1640EF,   /* W25Q32   (bl616dk)  */
};

static int lock_jedec_supported( void )
{
    extern uint32_t bflb_flash_get_jedec_id( void );
    uint32_t id = bflb_flash_get_jedec_id() & 0xFFFFFF;
    for( size_t i = 0; i < sizeof( k_lock_jedec_ok ) / sizeof( k_lock_jedec_ok[ 0 ] ); i++ )
    {
        if( k_lock_jedec_ok[ i ] == id ) { return 1; }
    }
    printf( "flash: jedec 0x%06X not in the write-protect whitelist - lock skipped (fail-open)\r\n",
            ( unsigned ) id );
    return 0;
}

static struct spi_flash_cfg_type * flash_cfg( void )
{
    uint8_t * cfg = 0;
    uint32_t  len = 0;
    bflb_flash_get_cfg( &cfg, &len );
    return ( struct spi_flash_cfg_type * ) cfg;
}

/* ONE status-register transaction, XIP-quiesced (BL602). do_write != 0 first 16-bit-WRSRs {w1,w2};
 * then always reads back SR1/SR2 into *r1/*r2. Returns 1 on success (cfg available). Runs from TCM;
 * everything it calls inside the XIP-off window (bflb_xip_sflash_* / bflb_sflash_*) is also TCM/ROM. */
static int BF_TCM bl602_sr_txn( int do_write, uint8_t w1, uint8_t w2, uint8_t * r1, uint8_t * r2 )
{
    struct spi_flash_cfg_type * cfg = flash_cfg();
    uint8_t  s1 = 0, s2 = 0, pair[ 2 ];

    if( cfg == 0 )
    {
        if( r1 ) *r1 = 0;
        if( r2 ) *r2 = 0;
        return 0;
    }

#if defined( BL602 ) || defined( BL616 ) || defined( BL616CL )
    {
        uintptr_t flag = bflb_irq_save();
        uint8_t   aes  = 0;
        uint32_t  off  = 0;

        bflb_xip_sflash_opt_enter( &aes );
        bflb_xip_sflash_state_save( cfg, &off, 0, 0 );

        if( do_write )
        {
            pair[ 0 ] = w1;
            pair[ 1 ] = w2;
            bflb_sflash_write_enable( cfg );   /* WRSR is ignored without a preceding WREN */
            bflb_sflash_write_reg_with_cmd( cfg, SF_CMD_WRSR, pair, 2 );
        }
        bflb_sflash_read_reg_with_cmd( cfg, SF_CMD_RDSR1, &s1, 1 );
        bflb_sflash_read_reg_with_cmd( cfg, SF_CMD_RDSR2, &s2, 1 );

#if defined( BL602 )
        bflb_xip_sflash_state_restore_ext( cfg, off, 0, 0 );
#else
        bflb_xip_sflash_state_restore( cfg, off, 0, 0 );
#endif
        bflb_xip_sflash_opt_exit( aes );
        bflb_irq_restore( flag );
    }
#else
    /* Any other Bouffalo chip without a wired-up XIP-quiesce path: plain access (untested). */
    if( do_write )
    {
        pair[ 0 ] = w1;
        pair[ 1 ] = w2;
        bflb_sflash_write_enable( cfg );
        bflb_sflash_write_reg_with_cmd( cfg, SF_CMD_WRSR, pair, 2 );
    }
    bflb_sflash_read_reg_with_cmd( cfg, SF_CMD_RDSR1, &s1, 1 );
    bflb_sflash_read_reg_with_cmd( cfg, SF_CMD_RDSR2, &s2, 1 );
#endif

    if( r1 ) *r1 = s1;
    if( r2 ) *r2 = s2;
    return 1;
}

int board_flash_read_status( uint8_t * sr1, uint8_t * sr2 )
{
    return bl602_sr_txn( 0, 0, 0, sr1, sr2 );
}

int board_flash_protect( board_flash_prot_t mode,
                         uint8_t * o_sr1b, uint8_t * o_sr1a, uint8_t * o_sr2b, uint8_t * o_sr2a )
{
    uint8_t sr1_cur = 0, sr2_cur = 0, w1, w2, v1 = 0, v2 = 0;

    /* LOCK on an unverified flash can shield the WRONG region (incl. LittleFS) — demote it to
     * an UNLOCK: that both skips the bad lock AND clears any protection a previous (unfixed)
     * firmware already left in the part's persistent SR bits. UNLOCK itself is safe anywhere. */
    if( ( mode != BOARD_FLASH_UNLOCK ) && !lock_jedec_supported() )
    {
        mode = BOARD_FLASH_UNLOCK;
    }

    if( !bl602_sr_txn( 0, 0, 0, &sr1_cur, &sr2_cur ) )   /* read current SR (quiesced) */
    {
        return 0;
    }

    board_flash_sr_for_mode( mode, sr2_cur, &w1, &w2 );   /* shared mode->SR encoding */

    bl602_sr_txn( 1, w1, w2, &v1, &v2 );        /* write + read back (quiesced) */

    if( o_sr1b ) *o_sr1b = sr1_cur;
    if( o_sr1a ) *o_sr1a = v1;
    if( o_sr2b ) *o_sr2b = sr2_cur;
    if( o_sr2a ) *o_sr2a = v2;
    return ( v1 == w1 ) && ( v2 == w2 );
}
