/*
 * vfs_lfs_mount — the cross-platform fd-VFS mount hook (component/syscall_config
 * syscalls.h) for Bouffalo. The shared newlib retarget's file backend routes
 * _open'd paths to the LittleFS instance this returns.
 *
 * This is the PSM LittleFS mount that used to live inside easyflash_init()
 * (component/lfs_easyflash, now removed): the same static lfs_context/lfs_config
 * fed to lfs_xip_init (this dir, component/bouffalo_sdk/lfs_port), which resolves the "PSM" partition and
 * caps block_count to the shared 240-block geometry. lfs_xip_init is a singleton
 * (its `if (lfs->cfg == cfg)` guard), so vfs_lfs_mount() is idempotent — safe to
 * call explicitly at boot (src/main.cpp) and lazily from the VFS on first open().
 *
 * Mirrors component/amebad/lfs_port/lfs_port.c:vfs_lfs_mount().
 */
#include "lfs.h"
#include "lfs_port.h"

static struct lfs_context lfs_ctx = { .partition_name = "PSM" };
static struct lfs_config  lfs_cfg = {
    .read_size      = 256,
    .prog_size      = 256,
    .block_size     = 4096,
    .cache_size     = 512,
    .lookahead_size = 256,
    .block_cycles   = 500,
};

/* Prototype is local: syscalls.h (the shared hook's declaration) is not on this
 * port's include path. Strong override of the shared core's weak contract. */
lfs_t *vfs_lfs_mount(void);
lfs_t *vfs_lfs_mount(void)
{
    return lfs_xip_init(&lfs_ctx, &lfs_cfg);
}
