/* SPDX-License-Identifier: Apache-2.0 */
#include "lfs_storage.h"
#include <string.h>

static int valid(lfs_block_t block, lfs_off_t off, lfs_size_t size)
{
    return block < TL_LFS_BLOCKS && off <= TL_FLASH_SECTOR &&
           size <= TL_FLASH_SECTOR - off && !(off % TL_LFS_CACHE) &&
           !(size % TL_LFS_CACHE);
}
static int read_block(const struct lfs_config *c, lfs_block_t block,
                      lfs_off_t off, void *data, lfs_size_t size)
{
    (void)c;
    if (!valid(block, off, size)) return LFS_ERR_INVAL;
    return tl_storage_read(TL_LFS_BASE + block * TL_FLASH_SECTOR + off, data, size);
}
static int prog_block(const struct lfs_config *c, lfs_block_t block,
                      lfs_off_t off, const void *data, lfs_size_t size)
{
    (void)c;
    if (!valid(block, off, size)) return LFS_ERR_INVAL;
    const uint8_t *p = data;
    uint32_t address = TL_LFS_BASE + block * TL_FLASH_SECTOR + off;
    while (size) {
        int rc = tl_storage_prog(address, p, TL_FLASH_PAGE);
        if (rc) return rc;
        address += TL_FLASH_PAGE; p += TL_FLASH_PAGE; size -= TL_FLASH_PAGE;
    }
    return 0;
}
static int erase_block(const struct lfs_config *c, lfs_block_t block)
{
    (void)c;
    if (block >= TL_LFS_BLOCKS) return LFS_ERR_INVAL;
    return tl_storage_erase(TL_LFS_BASE + block * TL_FLASH_SECTOR);
}
static int sync_device(const struct lfs_config *c) { (void)c; return 0; }
static int lock_device(const struct lfs_config *c) { (void)c; return tl_storage_lock(); }
static int unlock_device(const struct lfs_config *c) { (void)c; return tl_storage_unlock(); }

int tl_lfs_mount(TlLfs *s)
{
    if (s->mounted) return 0;
    memset(s, 0, sizeof *s);
    s->cfg = (struct lfs_config){
        .read = read_block, .prog = prog_block, .erase = erase_block,
        .sync = sync_device, .lock = lock_device, .unlock = unlock_device,
        .read_size = TL_LFS_CACHE, .prog_size = TL_LFS_CACHE,
        .block_size = TL_FLASH_SECTOR, .block_count = TL_LFS_BLOCKS,
        .cache_size = TL_LFS_CACHE, .lookahead_size = sizeof s->lookahead,
        .block_cycles = 500, .read_buffer = s->read_cache,
        .prog_buffer = s->prog_cache, .lookahead_buffer = s->lookahead,
    };
    int rc = lfs_mount(&s->fs, &s->cfg);
    if (rc == LFS_ERR_CORRUPT) {
        /* A transport failure or a nonblank corrupt filesystem must never
         * silently erase keys. Scan the WHOLE partition, not just its header. */
        for (uint32_t off = 0; off < TL_LFS_SIZE; off += TL_LFS_CACHE) {
            int io = tl_storage_read(TL_LFS_BASE + off, s->read_cache, TL_LFS_CACHE);
            if (io) { s->error = io; return io; }
            for (unsigned i = 0; i < TL_LFS_CACHE; ++i)
                if (s->read_cache[i] != 0xff) { s->error = rc; return rc; }
            if (!(off % TL_FLASH_SECTOR)) tl_storage_yield();
        }
        rc = lfs_format(&s->fs, &s->cfg);
        if (!rc) { s->formatted = 1; rc = lfs_mount(&s->fs, &s->cfg); }
    }
    s->error = rc;
    s->mounted = !rc;
    return rc;
}
void tl_lfs_unmount(TlLfs *s)
{
    if (s->mounted) (void)lfs_unmount(&s->fs);
    s->mounted = 0;
}
