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

unset(_lfs_dir)
