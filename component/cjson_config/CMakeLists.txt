# cjson_config — DaveGamble/cJSON v1.7.19+ (submodule component/cjson_config/cjson),
# static-only.
#
# Submodule replaces the Bouffalo-vendored copy that lived at
# bouffalo_sdk/components/utils/cjson/ — byte-identical to upstream, a drop-in.
#
# Embedded build settings: no shared lib, no tests, no install/export targets, no custom
# compiler flags (upstream's -Werror breaks the t-head riscv toolchain), no locale
# support (newlib-nano on Bouffalo / Realtek targets ships no full locale data).
#
# Targets produced: `cjson` (STATIC) and `cjson-utils` (STATIC, for JSON Patch / Pointer
# / Merge — Bouffalo's CMakeLists pulled cJSON_Utils.c in so we mirror that).

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

# Public headers live at the submodule root (cJSON.h, cJSON_Utils.h); upstream sets that
# via target_include_directories on the cjson target.
