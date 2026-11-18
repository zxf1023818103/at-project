# component/bouffalo_sdk/lfs_port — Bouffalo LittleFS flash port for the shared LittleFS
# service (component/littlefs_config). Supplies the four flash diskio callbacks over the bflb
# flash HAL + the vfs_lfs_mount() hook via lfs_config_mount() — the same mount path AmebaD uses
# (component/amebad/lfs_port), so the whole VFS + df/chmod + mkfs (lfs_config_format) share one
# consistent mount. Target `lfs_port` (unified with AmebaD).
add_library(lfs_port STATIC vfs_lfs_mount.c)

# littlefs (lfs.h; it carries the PUBLIC LFS_THREADSAFE) + lfs_config (geometry + the mount
# lifecycle + the giant lock). bflb_lhal is the flash HAL (bflb_flash.h) — implementation detail.
target_link_libraries(lfs_port PUBLIC  littlefs lfs_config)
target_link_libraries(lfs_port PRIVATE bflb_lhal)
