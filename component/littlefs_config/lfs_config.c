#include "lfs_config.h"
#include "../ble_store_config/lfs_blank.h"

#include "FreeRTOS.h"
#include "semphr.h"

void lfs_config_fill(struct lfs_config *cfg)
{
    cfg->read_size      = LFS_CFG_READ_SIZE;
    cfg->prog_size      = LFS_CFG_PROG_SIZE;
    cfg->block_size     = LFS_CFG_BLOCK_SIZE;
    cfg->block_count    = LFS_CFG_BLOCK_COUNT;
    cfg->cache_size     = LFS_CFG_CACHE_SIZE;
    cfg->lookahead_size = LFS_CFG_LOOKAHEAD_SIZE;
    cfg->block_cycles   = LFS_CFG_BLOCK_CYCLES;
}

/* ===== LFS_THREADSAFE lock/unlock ========================================= *
 * littlefs wraps each top-level op in lock/unlock (no nesting), so a plain
 * (non-recursive) FreeRTOS mutex is enough. Created lazily on first use. */
static SemaphoreHandle_t s_lfs_mutex;

static int lfs_cfg_lock(const struct lfs_config *c)
{
    (void)c;
    if (!s_lfs_mutex) return LFS_ERR_NOMEM;
    return xSemaphoreTake(s_lfs_mutex,portMAX_DELAY)==pdTRUE ? 0 : LFS_ERR_IO;
}

static int lfs_cfg_unlock(const struct lfs_config *c)
{
    (void)c;
    if (s_lfs_mutex != NULL) {
        xSemaphoreGive(s_lfs_mutex);
    }
    return LFS_ERR_OK;
}

/* ===== mount singleton ==================================================== */

static lfs_t            s_lfs;
static struct lfs_config s_cfg;
static int              s_mounted;

static void build_config(const struct lfs_diskio *io)
{
    /* geometry + RAM params (cross-platform) */
    lfs_config_fill(&s_cfg);
    /* platform diskio */
    s_cfg.read  = io->read;
    s_cfg.prog  = io->prog;
    s_cfg.erase = io->erase;
    s_cfg.sync  = io->sync;
#ifdef LFS_THREADSAFE
    s_cfg.lock   = lfs_cfg_lock;
    s_cfg.unlock = lfs_cfg_unlock;
#endif
}

lfs_t *lfs_config_mount(const struct lfs_diskio *io)
{
    int err;

    if (s_mounted) {
        return &s_lfs;
    }
    if (!s_lfs_mutex) s_lfs_mutex=xSemaphoreCreateMutex();
    if (!s_lfs_mutex) return NULL;
    build_config(io);

    err = lfs_mount(&s_lfs, &s_cfg);
    if (err == LFS_ERR_CORRUPT && lfs_region_is_blank(&s_cfg)==1) {
        err=lfs_format(&s_lfs,&s_cfg);
        if (!err) err=lfs_mount(&s_lfs,&s_cfg);
    }
    if (err) return NULL;
    s_mounted = 1;
    return &s_lfs;
}

void lfs_config_unmount(void)
{
    if (s_mounted) {
        lfs_unmount(&s_lfs);
        s_mounted = 0;
    }
}

int lfs_config_format(void)
{
    int err;

    /* A prior mount attempt prepares s_cfg even if the medium is corrupt.
     * Reject an uninitialized config before littlefs dereferences its callbacks. */
    if (!s_cfg.read || !s_cfg.prog || !s_cfg.erase || !s_cfg.sync) {
        return LFS_ERR_INVAL;
    }
    if (s_mounted) {
        lfs_unmount(&s_lfs);
        s_mounted = 0;
    }
    err = lfs_format(&s_lfs, &s_cfg);
    if (err == LFS_ERR_OK) {
        err = lfs_mount(&s_lfs, &s_cfg);
        if (err == LFS_ERR_OK) {
            s_mounted = 1;
        }
    }
    return err;
}
