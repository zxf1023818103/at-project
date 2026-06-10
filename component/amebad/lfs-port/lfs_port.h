/*
 * lfs-port (AmebaD) — the low-level flash adapter that backs LittleFS on the
 * RTL8721D. Implements the four lfs_config diskio callbacks (read / prog / erase
 * / sync) over the SDK flash HAL (flash_api.h) at the unified 0x300000 LFS region
 * (see component/lfs_config / doc/lfs.md), plus the lock/unlock callbacks
 * required by LFS_THREADSAFE. The geometry / runtime parameters come from
 * lfs_config; this component only supplies the platform-specific I/O + a mount
 * helper.
 */
#ifndef AT_PROJECT_AMEBAD_LFS_PORT_H
#define AT_PROJECT_AMEBAD_LFS_PORT_H

#include "lfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Mount the on-flash LittleFS at the 0x300000 region. If the medium is blank or
 * corrupt (e.g. a firmware-only image flashed without lfs.bin), it is formatted
 * once and re-mounted, so the caller always gets a usable filesystem. Returns a
 * pointer to the mounted instance (a single static lfs_t), or NULL on a hard
 * flash/format error. Mounting twice without unmounting returns the same
 * already-mounted instance.
 */
lfs_t *amebad_lfs_mount(void);

/* Unmount (lfs_unmount). Safe to pass NULL. */
void amebad_lfs_unmount(lfs_t *lfs);

/* diskio callbacks — wired into lfs_config by amebad_lfs_mount(); exposed for
 * inspection / reuse. Addresses are relative to the LFS region base. */
int amebad_lfs_read (const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size);
int amebad_lfs_prog (const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size);
int amebad_lfs_erase(const struct lfs_config *c, lfs_block_t block);
int amebad_lfs_sync (const struct lfs_config *c);

#ifdef __cplusplus
}
#endif

#endif /* AT_PROJECT_AMEBAD_LFS_PORT_H */
