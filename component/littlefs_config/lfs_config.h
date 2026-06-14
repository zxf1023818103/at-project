/*
 * lfs_config — the cross-platform LittleFS parameter set from doc/lfs.md, in one
 * place so a single lfs.bin mounts on every platform (Bouffalo / Ameba D / Dplus
 * / Green2). It owns the geometry + runtime config ONLY; the flash diskio
 * callbacks (read/prog/erase/sync, and lock/unlock under LFS_THREADSAFE) plus the
 * lfs_config.context are each platform port's responsibility.
 *
 * Disk-format-bound (read_size / prog_size / block_size / block_count): changing
 * any of these makes existing images unreadable. RAM / behaviour (cache_size /
 * lookahead_size / block_cycles): not stored on disk — unified across platforms
 * only for simplicity.
 */
#ifndef AT_PROJECT_LFS_CONFIG_H
#define AT_PROJECT_LFS_CONFIG_H

#include "lfs.h"

#define LFS_CFG_READ_SIZE       256u
#define LFS_CFG_PROG_SIZE       256u
#define LFS_CFG_BLOCK_SIZE      4096u
#define LFS_CFG_CACHE_SIZE      512u
#define LFS_CFG_LOOKAHEAD_SIZE  256u
#define LFS_CFG_BLOCK_CYCLES    500

/* LFS partition is identical on every platform: raw flash offset 0x300000, size
 * 960 KB = 240 * 4 KB blocks, ending at 0x3F0000. The top 64 KB (0x3F0000-0x400000)
 * is RESERVED for platform secure storage — on Ameba D it holds the TF-M PSA
 * ITS/PS/NV-counter areas (see doc/amebad_tfm_storage.md §2); on the other
 * platforms it is simply left blank. Kept uniform across platforms so a single
 * lfs.bin (formatted at 240 blocks) mounts everywhere. See doc/lfs.md /
 * flash_layout.md. NB block_count is recorded in the LFS superblock, so this
 * value must match the image mklittlefs produces (IMAGE_SIZE in
 * component/lfs_image/CMakeLists.txt). */
#define LFS_CFG_REGION_OFFSET   0x300000u
#define LFS_CFG_REGION_SIZE     0x0F0000u
#define LFS_CFG_BLOCK_COUNT     (LFS_CFG_REGION_SIZE / LFS_CFG_BLOCK_SIZE)

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Fill the geometry + RAM fields of *cfg from the constants above. Leaves the
 * diskio callbacks, the lock/unlock callbacks, and cfg->context untouched — the
 * mount lifecycle below sets those before lfs_mount().
 */
void lfs_config_fill(struct lfs_config *cfg);

/*
 * The flash diskio a platform port supplies (addresses relative to the LFS region
 * base). The geometry/RAM params (lfs_config_fill) AND the mount lifecycle +
 * LFS_THREADSAFE lock/unlock are owned here; a port provides only these four.
 */
struct lfs_diskio {
    int (*read )(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size);
    int (*prog )(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size);
    int (*erase)(const struct lfs_config *c, lfs_block_t block);
    int (*sync )(const struct lfs_config *c);
};

/*
 * Mount the shared singleton over `io` at the unified LFS region. A blank or
 * corrupt medium (e.g. a firmware-only image flashed without lfs.bin) is formatted
 * once and re-mounted, so the caller always gets a usable filesystem. Idempotent:
 * a second call without unmount returns the same instance. Returns a pointer to the
 * mounted instance (a single static lfs_t), or NULL on a hard flash/format error.
 */
lfs_t *lfs_config_mount(const struct lfs_diskio *io);

/* Unmount the shared singleton (a no-op if it was never mounted). */
void lfs_config_unmount(void);

#ifdef __cplusplus
}
#endif

#endif /* AT_PROJECT_LFS_CONFIG_H */
