include_guard()

# Wires component/littlefs (upstream littlefs-project/littlefs v2.8.2) into
# the chip-toolchain build. Upstream ships no CMakeLists — only a Makefile
# that builds `liblfs.a` — so we add the lib here.
#
# Target produced:
#   littlefs   STATIC, EXCLUDE_FROM_ALL. Exports the submodule root on
#              the include path so consumers see <lfs.h>, <lfs_util.h>.
#
# Consumers (bflb_littlefs_port, lfs_easyflash, the Ameba adapters when
# they get extracted) link this and get both the symbols and the headers.
#
# Required environment:
#   AT_PROJECT_ROOT set.

if(NOT DEFINED AT_PROJECT_ROOT)
    message(FATAL_ERROR "AT_PROJECT_ROOT must be set before including cmake/littlefs.cmake")
endif()

set(_lfs_dir "${AT_PROJECT_ROOT}/component/littlefs")

add_library(littlefs STATIC
    "${_lfs_dir}/lfs.c"
    "${_lfs_dir}/lfs_util.c"
)

target_include_directories(littlefs PUBLIC
    "${_lfs_dir}"
)

# Upstream's lfs.c / lfs_util.c trigger upstream-acknowledged warnings
# under -Wall -Wextra. Silence them on the lfs target only — same
# treatment as zlib / cjson upstream sources.
target_compile_options(littlefs PRIVATE -w)

# LFS_THREADSAFE changes sizeof(struct lfs_config) by adding lock/unlock fn
# ptrs. It MUST be defined identically wherever lfs.h is included, or static
# instances initialized in one TU get read with shifted field offsets in
# another — silently corrupts pointers like read_buffer to whatever the
# preceding lfs_size_t field happened to be. Pin it PUBLIC on `littlefs`
# itself so both lfs.c (this lib's own source) and every consumer of lfs.h
# (bflb_littlefs_port, lfs_easyflash, future Ameba adapters) agree.
target_compile_definitions(littlefs PUBLIC LFS_THREADSAFE)

unset(_lfs_dir)
