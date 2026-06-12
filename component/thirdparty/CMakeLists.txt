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
#   1. git submodule add <url> component/thirdparty/<name>
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

# madler/zlib's CMakeLists hard-codes `add_library(zlib SHARED ...)` (no option
# to skip it). Our cross targets have no shared-lib loader, so every configure
# CMake prints a dev warning ("ADD_LIBRARY called with SHARED option but the
# target platform does not support dynamic linking. Building a STATIC library
# instead."). That SHARED target is dead code here (EXCLUDE_FROM_ALL — only
# zlibstatic is ever linked), so the warning is pure noise. It is gated on the
# global TARGET_SUPPORTS_SHARED_LIBS property; flip it TRUE just across this
# add_subdirectory so the SHARED target is accepted silently instead of warning
# + falling back, then restore the real value so nothing else is affected (the
# dead target is never built, so declaring it SHARED has no effect).
get_property(_at_tsl_save GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS)
set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS TRUE)

add_subdirectory(zlib
                 ${CMAKE_BINARY_DIR}/at_zlib_build
                 EXCLUDE_FROM_ALL)

set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS "${_at_tsl_save}")

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

# littlefs — upstream littlefs-project/littlefs v2.8.2 (a submodule that ships no
# CMakeLists). The wrapper add_library lives in its own subdir (littlefs_lib) so this
# umbrella stays add_subdirectory-only; it still produces the `littlefs` STATIC target.
add_subdirectory(littlefs_lib)
