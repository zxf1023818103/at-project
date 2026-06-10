/*
 * lfs-config — the cross-platform LittleFS parameter set from doc/lfs.md, in one
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

/* LFS partition is identical on every platform: raw flash offset 0x300000 to the
 * 4 MB flash end = 1024 KB = 256 * 4 KB blocks. See doc/lfs.md / flash_layout.md. */
#define LFS_CFG_REGION_OFFSET   0x300000u
#define LFS_CFG_REGION_SIZE     0x100000u
#define LFS_CFG_BLOCK_COUNT     (LFS_CFG_REGION_SIZE / LFS_CFG_BLOCK_SIZE)

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Fill the geometry + RAM fields of *cfg from the constants above. Leaves the
 * diskio callbacks, the lock/unlock callbacks, and cfg->context untouched — the
 * caller (platform port) sets those before lfs_mount().
 */
void lfs_config_fill(struct lfs_config *cfg);

#ifdef __cplusplus
}
#endif

#endif /* AT_PROJECT_LFS_CONFIG_H */
