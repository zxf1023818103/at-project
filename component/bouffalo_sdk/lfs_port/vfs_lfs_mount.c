/*
 * vfs_lfs_mount — the Bouffalo LittleFS flash diskio + the cross-platform fd-VFS mount hook
 * (component/syscall_config syscalls.h). Supplies the four flash callbacks over the bflb flash
 * HAL to the shared component/littlefs_config, then mounts the shared singleton via
 * lfs_config_mount() — the SAME mount path AmebaD uses (component/amebad/lfs_port/lfs_port.c).
 * So the whole VFS + df + chmod + mkfs (lfs_config_format) operate on one consistent instance;
 * previously the mount went through bflb's private lfs_xip_init (its own lfs_config), which left
 * lfs_config_format() — the mkfs command — talking to a different, unmounted config.
 *
 * The LFS region is at raw SPI-NOR offset 0x300000 (LFS_CFG_REGION_OFFSET) on every Bouffalo
 * board (PSM partition address0 = 0x300000 in bl602dk / bl616dk / bl616cldk partition_cfg_4M.toml),
 * so — like AmebaD — the base is a compile constant, not a runtime bflb_mtd lookup.
 */
#include "lfs.h"
#include "lfs_config.h"
#include "bflb_flash.h"      /* bflb_flash_read / write / erase — raw SPI-NOR offsets */

/* Raw SPI-NOR offset of the LFS region (not the 0x23000000 XIP alias). */
#define LFS_BASE   LFS_CFG_REGION_OFFSET

static int bflb_lfs_read( const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                          void *buffer, lfs_size_t size )
{
    return bflb_flash_read( LFS_BASE + c->block_size * block + off, ( uint8_t * ) buffer, size );
}

static int bflb_lfs_prog( const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                          const void *buffer, lfs_size_t size )
{
    return bflb_flash_write( LFS_BASE + c->block_size * block + off, ( uint8_t * ) buffer, size );
}

static int bflb_lfs_erase( const struct lfs_config *c, lfs_block_t block )
{
    return bflb_flash_erase( LFS_BASE + c->block_size * block, c->block_size );
}

static int bflb_lfs_sync( const struct lfs_config *c )
{
    ( void ) c;
    return 0;   /* writes are synchronous through the flash HAL */
}

static const struct lfs_diskio bflb_lfs_diskio = {
    bflb_lfs_read, bflb_lfs_prog, bflb_lfs_erase, bflb_lfs_sync,
};

/* Cross-platform fd-VFS mount hook. Idempotent (lfs_config_mount is a singleton), so it is
 * safe to call explicitly at boot (src/main.cpp) and lazily from the VFS on first open().
 * The prototype is local (syscalls.h is not on this port's include path). Mirrors
 * component/amebad/lfs_port/lfs_port.c:vfs_lfs_mount(). */
lfs_t *vfs_lfs_mount( void );
lfs_t *vfs_lfs_mount( void )
{
    return lfs_config_mount( &bflb_lfs_diskio );
}
