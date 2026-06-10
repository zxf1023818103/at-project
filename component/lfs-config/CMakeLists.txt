# component/lfs-config — cross-platform LittleFS parameter set (doc/lfs.md).
# Owns the geometry + runtime config so one lfs.bin mounts on every platform;
# the diskio callbacks come from each platform's lfs port. Links littlefs only
# for the struct lfs_config / lfs.h definitions. EXCLUDE_FROM_ALL: built only
# when a port links it (no consumer on platforms that use their own adapter).
add_library(lfs_config STATIC EXCLUDE_FROM_ALL lfs_config.c)
target_include_directories(lfs_config PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(lfs_config PUBLIC littlefs)
