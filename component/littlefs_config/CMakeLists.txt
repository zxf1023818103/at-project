# component/littlefs_config — the upstream littlefs submodule
# (component/littlefs_config/littlefs, v2.8.2, ships only a Makefile) wrapped into the
# `littlefs` STATIC target, PLUS the cross-platform LittleFS service layer (the
# `lfs_config` target — formerly component/lfs_config).
#
# Targets produced:
#   littlefs     STATIC. lfs.c + lfs_util.c from the submodule; exports <lfs.h>,
#                <lfs_util.h>.
#   lfs_config   STATIC, EXCLUDE_FROM_ALL. The geometry + runtime config (doc/lfs.md) so
#                one lfs.bin mounts on every platform, the mount lifecycle
#                (lfs_config_mount/unmount, format-on-blank), and the LFS_THREADSAFE
#                lock (a FreeRTOS mutex). Each platform's lfs port supplies only the four
#                flash diskio callbacks (struct lfs_diskio).

# ---- littlefs: upstream submodule (no CMakeLists of its own) ----
set(_lfs "${CMAKE_CURRENT_LIST_DIR}/littlefs")
add_library(littlefs STATIC
    "${_lfs}/lfs.c"
    "${_lfs}/lfs_util.c"
)
target_include_directories(littlefs PUBLIC "${_lfs}")
# Upstream lfs.c / lfs_util.c trigger upstream-acknowledged warnings under -Wall
# -Wextra. Silence on the lfs target only — same treatment as zlib / cjson.
target_compile_options(littlefs PRIVATE -w)
# LFS_THREADSAFE changes sizeof(struct lfs_config) by adding lock/unlock fn ptrs. It
# MUST be defined identically wherever lfs.h is included, or static instances get read
# with shifted field offsets in another TU — silently corrupts pointers. Pin it PUBLIC
# on `littlefs` so lfs.c and every consumer of lfs.h agree.
target_compile_definitions(littlefs PUBLIC LFS_THREADSAFE)

# ---- lfs_config: cross-platform LittleFS service layer ----
# lfs_selftest.c: portable LittleFS self-test (mount via the platform's vfs_lfs_mount
# hook, list/read/write round-trip). It also carries the optional POSIX-fd VFS self-test
# (vfs_selftest, #ifdef AMEBAD_VFS_SELFTEST — the consumer attaches FreeRTOS+TCP +
# syscall_config when the flag is set). The C++ std::filesystem/iostream self-test
# (cpp_selftest.cpp) is added conditionally by the consumer (CXX-only, gated).
add_library(lfs_config STATIC EXCLUDE_FROM_ALL lfs_config.c lfs_selftest.c)
target_include_directories(lfs_config PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# littlefs: struct lfs_config / lfs_mount/format/unmount. FreeRTOS: the threadsafe lock's
# mutex (semphr.h) — header-only borrow, the kernel archive is in the image.
target_link_libraries(lfs_config PUBLIC littlefs)
target_link_libraries(lfs_config PRIVATE FreeRTOS)
