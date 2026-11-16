/*
 * lfs_port (AmebaGreen2) — the low-level flash adapter that backs LittleFS on the
 * RTL8711Dx. Implements the four lfs_config diskio callbacks (read / prog / erase
 * / sync) over the SDK flash HAL (flash_api.h) at the unified 0x300000 LFS region
 * (see component/littlefs_config / doc/lfs.md), plus the lock/unlock callbacks
 * required by LFS_THREADSAFE. The geometry / runtime parameters come from
 * lfs_config; this component only supplies the platform-specific I/O + a mount
 * helper.
 */
#ifndef AT_PROJECT_AMEBAGREEN2_LFS_PORT_H
#define AT_PROJECT_AMEBAGREEN2_LFS_PORT_H

#include "lfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Mount the on-flash LittleFS at the 0x300000 region. If the medium is blank (e.g. a firmware-only image flashed without lfs.bin), it is formatted
 * once and re-mounted, while a corrupt nonblank filesystem is preserved. Returns a
 * pointer to the mounted instance (a single static lfs_t), or NULL on a hard
 * flash/format error. Mounting twice without unmounting returns the same
 * already-mounted instance.
 */
lfs_t *vfs_lfs_mount(void);   /* the cross-platform mount entry (same name as bouffalo's lfs_port) */

/* Unmount (lfs_unmount). Safe to pass NULL. */
void amebagreen2_lfs_unmount(lfs_t *lfs);

/* diskio callbacks — wired into lfs_config by vfs_lfs_mount(); exposed for
 * inspection / reuse. Addresses are relative to the LFS region base. */
int amebagreen2_lfs_read (const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size);
int amebagreen2_lfs_prog (const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size);
int amebagreen2_lfs_erase(const struct lfs_config *c, lfs_block_t block);
int amebagreen2_lfs_sync (const struct lfs_config *c);

#ifdef __cplusplus
}
#endif

#endif /* AT_PROJECT_AMEBAGREEN2_LFS_PORT_H */
