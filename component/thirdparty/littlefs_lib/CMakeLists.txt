# littlefs_lib — CMake wrapper for the upstream littlefs-project/littlefs v2.8.2
# submodule (component/thirdparty/littlefs), which ships only a Makefile. Lives in
# its own subdir (a submodule can't hold our CMakeLists) so the component/thirdparty
# umbrella stays add_subdirectory-only.
#
# Target produced:
#   littlefs   STATIC. Exports the submodule root on the include path so consumers
#              see <lfs.h>, <lfs_util.h>.
set(_lfs "${CMAKE_CURRENT_LIST_DIR}/../littlefs")

add_library(littlefs STATIC
    "${_lfs}/lfs.c"
    "${_lfs}/lfs_util.c"
)

target_include_directories(littlefs PUBLIC "${_lfs}")

# Upstream's lfs.c / lfs_util.c trigger upstream-acknowledged warnings under
# -Wall -Wextra. Silence them on the lfs target only — same treatment as zlib / cjson.
target_compile_options(littlefs PRIVATE -w)

# LFS_THREADSAFE changes sizeof(struct lfs_config) by adding lock/unlock fn ptrs. It
# MUST be defined identically wherever lfs.h is included, or static instances
# initialized in one TU get read with shifted field offsets in another — silently
# corrupts pointers like read_buffer. Pin it PUBLIC on `littlefs` so lfs.c (this lib's
# own source) and every consumer of lfs.h (bflb_littlefs_port, lfs_easyflash, the
# Ameba lfs_port) agree.
target_compile_definitions(littlefs PUBLIC LFS_THREADSAFE)
