# Third-party submodule wiring. Cross-platform only — each block sets
# the upstream's option knobs (BUILD_SHARED_LIBS, ENABLE_*),
# add_subdirectory's the source, and applies warning-silencing on the
# produced targets.
#
# What is NOT here:
#   - umbrella stitching into the `bouffalo_sdk` INTERFACE target
#   - the upstream lwip / mbedtls pulls, which need Bouffalo port-adapter
#     headers (lwipopts.h, config-tls-generic.h) to even compile.
# Both live on the Bouffalo side — see component/bouffalo_sdk/CMakeLists.txt
# and component/bouffalo_sdk/bflb_lwip_port/CMakeLists.txt. This keeps
# the dependency one-directional: the SDK pulls the third-party libs it
# needs, the third-party libs don't peek at the SDK.
#
# freertos-kernel is NOT add_subdirectory'd here either: it's consumed
# by component/bouffalo_sdk/CMakeLists.txt (which feeds it FREERTOS_PORT
# + freertos_config and adds the kernel directly).
#
# To add a new cross-platform submodule:
#   1. git submodule add <url> component/thridparty/<name>
#   2. add a block below — option knobs, add_subdirectory(<name> ...),
#      warning-silence.
#   3. wire the target into the consuming project via target_link_libraries.

# ---------------------------------------------------------------------
# zlib — madler/zlib v1.3.1, static-only.
# ---------------------------------------------------------------------
#
# Using madler/zlib (the upstream reference). It uses plain `int`
# everywhere in its public API, so we don't trip the `int32_t vs int`
# type-aliasing mismatches that arm-none-eabi-gcc 12 has (where
# __INT32_TYPE__=long int and __SIZE_TYPE__=unsigned int — both 32 bits
# wide but treated as distinct types by the compiler). zlib-ng's compat
# and native APIs both break under that flavor of typedef choices.
#
# madler/zlib's CMakeLists always emits BOTH targets:
#   - zlib       SHARED   (we don't build this — embedded targets have
#                          no shared-lib loader anyway)
#   - zlibstatic STATIC   (what we link against)
# With EXCLUDE_FROM_ALL only the target we link to (zlibstatic) gets
# pulled into the build, so the SHARED target is effectively dead code.

set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# madler/zlib's CMakeLists runs check_function_exists / check_c_source
# that default to a tiny executable link. Our cross toolchains need
# -nostartfiles + a custom linker script and can't link a bare exe.
# Make the compile checks fall back to STATIC_LIBRARY.
set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")

add_subdirectory(zlib
                 ${CMAKE_BINARY_DIR}/at_zlib_build
                 EXCLUDE_FROM_ALL)

# zlib's source uses constructs that trip the Ameba SDK's strict
# -Wundef -Wall -Wextra -Werror flag stack (`#if BIG_ENDIAN`, signedness
# comparisons, unused vars, etc.). Silence them on the zlib target only.
target_compile_options(zlibstatic PRIVATE -w)

# ---------------------------------------------------------------------
# cjson — DaveGamble/cJSON v1.7.19+, static-only.
# ---------------------------------------------------------------------
#
# Submodule replaces the Bouffalo-vendored copy that lived at
# bouffalo_sdk/components/utils/cjson/ — files are byte-identical to
# upstream, so this is a drop-in.
#
# Embedded build settings: no shared lib, no tests, no install/export
# targets, no custom compiler flags (upstream adds -Werror that breaks
# the t-head riscv toolchain), no locale support (newlib-nano on Bouffalo
# / Realtek embedded targets doesn't ship full locale data).
#
# Targets produced: `cjson` (STATIC) and `cjson-utils` (STATIC, for
# JSON Patch / Pointer / Merge — Bouffalo's CMakeLists pulled
# cJSON_Utils.c in so we mirror that).

set(BUILD_SHARED_LIBS              OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_AND_STATIC_LIBS   OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_UTILS             ON  CACHE BOOL "" FORCE)
set(ENABLE_CJSON_TEST              OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_UNINSTALL         OFF CACHE BOOL "" FORCE)
set(ENABLE_TARGET_EXPORT           OFF CACHE BOOL "" FORCE)
set(ENABLE_CUSTOM_COMPILER_FLAGS   OFF CACHE BOOL "" FORCE)
set(ENABLE_LOCALES                 OFF CACHE BOOL "" FORCE)

add_subdirectory(cjson
                 ${CMAKE_BINARY_DIR}/at_cjson_build
                 EXCLUDE_FROM_ALL)

# Public headers live at the submodule root (cJSON.h, cJSON_Utils.h).
# Upstream sets that via target_include_directories on the cjson target.

# ---------------------------------------------------------------------
# littlefs — upstream littlefs-project/littlefs v2.8.2.
# ---------------------------------------------------------------------
#
# Upstream ships no CMakeLists — only a Makefile that builds liblfs.a —
# so we add the lib here.
#
# Target produced:
#   littlefs   STATIC. Exports the submodule root on the include path so
#              consumers see <lfs.h>, <lfs_util.h>.

add_library(littlefs STATIC
    "${CMAKE_CURRENT_LIST_DIR}/littlefs/lfs.c"
    "${CMAKE_CURRENT_LIST_DIR}/littlefs/lfs_util.c"
)

target_include_directories(littlefs PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/littlefs"
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
