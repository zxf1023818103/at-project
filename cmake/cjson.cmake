include_guard()

# Pulls in component/cjson (DaveGamble/cJSON v1.7.19+) as a static-only
# build. Submodule replaces the Bouffalo-vendored copy that lived at
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

# Same compile-test workaround as zlib — cross toolchain can't link a
# bare hello-world.
set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")

if(NOT DEFINED AT_PROJECT_ROOT)
    message(FATAL_ERROR "AT_PROJECT_ROOT must be set before including cmake/cjson.cmake")
endif()

add_subdirectory(${AT_PROJECT_ROOT}/component/cjson
                 ${CMAKE_BINARY_DIR}/at_cjson_build
                 EXCLUDE_FROM_ALL)

# Public headers live at the submodule root (cJSON.h, cJSON_Utils.h).
# Upstream sets that via target_include_directories on the cjson target —
# nothing to do here.

# Stitch into the bouffalo_sdk umbrella so downstream just links
# bouffalo_sdk and gets cjson transitively. Ameba projects don't define
# bouffalo_sdk, hence the guard.
if(TARGET bouffalo_sdk)
    target_link_libraries(bouffalo_sdk INTERFACE cjson cjson_utils)
endif()
