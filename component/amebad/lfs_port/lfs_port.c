#include "lfs_port.h"
#include "lfs_config.h"

#include "flash_api.h"      /* flash_t, flash_stream_read/write, flash_erase_sector */
#include "device_lock.h"    /* device_mutex_lock/unlock(RT_DEV_LOCK_FLASH) */

/* Absolute SPI-NOR offset of the LFS region (raw, 0-based — the flash HAL takes
 * raw offsets, not the 0x08000000 XIP alias). */
#define LFS_BASE   LFS_CFG_REGION_OFFSET

/* ===== diskio over the SDK flash HAL ====================================== *
 * Every op serialises on RT_DEV_LOCK_FLASH — the same device lock the WiFi
 * driver's flash backend uses — so concurrent flash users never interleave. */

int amebad_lfs_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                    void *buffer, lfs_size_t size)
{
    flash_t flash;

    if (size == 0) {
        return LFS_ERR_OK;
    }
    device_mutex_lock(RT_DEV_LOCK_FLASH);
    flash_stream_read(&flash, LFS_BASE + c->block_size * block + off, size,
                      (uint8_t *)buffer);
    device_mutex_unlock(RT_DEV_LOCK_FLASH);
    return LFS_ERR_OK;
}

int amebad_lfs_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                    const void *buffer, lfs_size_t size)
{
    flash_t flash;

    if (size == 0) {
        return LFS_ERR_OK;
    }
    if (off + size > c->block_size) {
        return LFS_ERR_IO;
    }
    device_mutex_lock(RT_DEV_LOCK_FLASH);
    flash_stream_write(&flash, LFS_BASE + c->block_size * block + off, size,
                       (uint8_t *)buffer);
    device_mutex_unlock(RT_DEV_LOCK_FLASH);
    return LFS_ERR_OK;
}

int amebad_lfs_erase(const struct lfs_config *c, lfs_block_t block)
{
    flash_t flash;

    /* block_size must be the NOR sector size — flash_erase_sector erases 4 KB. */
    if (c->block_size != 0x1000) {
        return LFS_ERR_IO;
    }
    device_mutex_lock(RT_DEV_LOCK_FLASH);
    flash_erase_sector(&flash, LFS_BASE + c->block_size * block);
    device_mutex_unlock(RT_DEV_LOCK_FLASH);
    return LFS_ERR_OK;
}

int amebad_lfs_sync(const struct lfs_config *c)
{
    (void)c;
    return LFS_ERR_OK;   /* writes are synchronous through the flash HAL */
}

/* ===== mount: delegate to the cross-platform lfs_config service =========== *
 * The mount lifecycle (singleton + format-on-blank) and the LFS_THREADSAFE
 * lock/unlock live in component/lfs_config; this port supplies only the four flash
 * diskio callbacks above. */
static const struct lfs_diskio amebad_lfs_diskio = {
    amebad_lfs_read, amebad_lfs_prog, amebad_lfs_erase, amebad_lfs_sync,
};

/* vfs_lfs_mount — the cross-platform mount entry + fd-VFS hook, SAME NAME as bouffalo's
 * lfs_port (component/bouffalo_sdk/lfs_port/vfs_lfs_mount.c): the newlib retarget's file
 * backend routes _open'd paths to this LittleFS instance. The prototype is local
 * (syscalls.h is not on this port's include path). Was amebad_lfs_mount + a wrapper. */
lfs_t *vfs_lfs_mount(void);
lfs_t *vfs_lfs_mount(void)
{
    return lfs_config_mount(&amebad_lfs_diskio);
}

void amebad_lfs_unmount(lfs_t *lfs)
{
    (void)lfs;                 /* singleton is owned by lfs_config */
    lfs_config_unmount();
}
