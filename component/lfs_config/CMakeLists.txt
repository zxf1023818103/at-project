# component/lfs_config — cross-platform LittleFS service (doc/lfs.md): the geometry
# + runtime config so one lfs.bin mounts on every platform, PLUS the mount lifecycle
# (lfs_config_mount/unmount over a singleton, format-on-blank) and the LFS_THREADSAFE
# lock/unlock (a FreeRTOS mutex). Each platform's lfs port supplies only the four
# flash diskio callbacks (struct lfs_diskio). EXCLUDE_FROM_ALL: built only when a
# port links it (no consumer on platforms that use their own adapter).
#
# lfs_selftest.c: portable LittleFS self-test (lfs_selftest() — mount via the
# platform's vfs_lfs_mount hook, list/read/write round-trip; output via printf). It
# also carries the optional POSIX-fd VFS self-test (vfs_selftest, #ifdef
# AMEBAD_VFS_SELFTEST — needs FreeRTOS+TCP sockets + syscall_config; the consumer
# attaches those when the flag is set). The C++ std::filesystem/iostream self-test
# (cpp_selftest.cpp) is added conditionally by the consumer (CXX-only, gated).
add_library(lfs_config STATIC EXCLUDE_FROM_ALL lfs_config.c lfs_selftest.c)
target_include_directories(lfs_config PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# littlefs: struct lfs_config / lfs_mount/format/unmount. FreeRTOS: the threadsafe
# lock's mutex (semphr.h) — header-only borrow, the kernel archive is in the image.
target_link_libraries(lfs_config PUBLIC littlefs)
target_link_libraries(lfs_config PRIVATE FreeRTOS)
