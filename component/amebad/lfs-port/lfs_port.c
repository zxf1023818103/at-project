#include "lfs_port.h"
#include "lfs_config.h"

#include "flash_api.h"      /* flash_t, flash_stream_read/write, flash_erase_sector */
#include "device_lock.h"    /* device_mutex_lock/unlock(RT_DEV_LOCK_FLASH) */

#include "FreeRTOS.h"
#include "semphr.h"

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

/* ===== LFS_THREADSAFE lock/unlock ========================================= *
 * littlefs wraps each top-level op in lock/unlock (no nesting), so a plain
 * (non-recursive) FreeRTOS mutex is enough. Created lazily on first use. */
static SemaphoreHandle_t s_lfs_mutex;

static int amebad_lfs_lock(const struct lfs_config *c)
{
    (void)c;
    if (s_lfs_mutex == NULL) {
        s_lfs_mutex = xSemaphoreCreateMutex();
    }
    if (s_lfs_mutex != NULL) {
        xSemaphoreTake(s_lfs_mutex, portMAX_DELAY);
    }
    return LFS_ERR_OK;
}

static int amebad_lfs_unlock(const struct lfs_config *c)
{
    (void)c;
    if (s_lfs_mutex != NULL) {
        xSemaphoreGive(s_lfs_mutex);
    }
    return LFS_ERR_OK;
}

/* ===== config + mount ===================================================== */

static lfs_t          s_lfs;
static struct lfs_config s_cfg;
static int            s_mounted;

static void build_config(void)
{
    /* geometry + RAM params from the cross-platform lfs-config */
    lfs_config_fill(&s_cfg);
    /* platform diskio + threadsafe locks */
    s_cfg.read  = amebad_lfs_read;
    s_cfg.prog  = amebad_lfs_prog;
    s_cfg.erase = amebad_lfs_erase;
    s_cfg.sync  = amebad_lfs_sync;
#ifdef LFS_THREADSAFE
    s_cfg.lock   = amebad_lfs_lock;
    s_cfg.unlock = amebad_lfs_unlock;
#endif
}

lfs_t *amebad_lfs_mount(void)
{
    int err;

    if (s_mounted) {
        return &s_lfs;
    }
    build_config();

    err = lfs_mount(&s_lfs, &s_cfg);
    if (err != LFS_ERR_OK) {
        /* Blank or corrupt medium (firmware-only image, no lfs.bin) — format
         * once and retry. A valid pre-flashed lfs.bin mounts on the first try
         * and never reaches here. */
        err = lfs_format(&s_lfs, &s_cfg);
        if (err != LFS_ERR_OK) {
            return NULL;
        }
        err = lfs_mount(&s_lfs, &s_cfg);
        if (err != LFS_ERR_OK) {
            return NULL;
        }
    }
    s_mounted = 1;
    return &s_lfs;
}

void amebad_lfs_unmount(lfs_t *lfs)
{
    if (lfs != NULL && s_mounted) {
        lfs_unmount(lfs);
        s_mounted = 0;
    }
}
