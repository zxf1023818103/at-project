# zlib_config — madler/zlib v1.3.1 (submodule component/zlib_config/zlib), static-only.
#
# Using madler/zlib (the upstream reference). It uses plain `int` everywhere in its
# public API, so we don't trip the int32_t-vs-int type-aliasing mismatches that
# arm-none-eabi-gcc has (__INT32_TYPE__=long int vs __SIZE_TYPE__=unsigned int — both
# 32-bit but distinct types). zlib-ng's compat and native APIs both break under that.
#
# madler/zlib's CMakeLists always emits BOTH targets:
#   - zlib       SHARED   (we don't build this — embedded targets have no loader)
#   - zlibstatic STATIC   (what we link against)
# With EXCLUDE_FROM_ALL only the linked target (zlibstatic) is pulled in.

set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# madler/zlib's CMakeLists runs check_function_exists / check_c_source that default to
# a tiny executable link. Our cross toolchains need -nostartfiles + a custom linker
# script and can't link a bare exe. Make the compile checks fall back to STATIC_LIBRARY.
set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")

# madler/zlib hard-codes `add_library(zlib SHARED ...)`. Our cross targets have no
# shared-lib loader, so configure prints a dev warning + falls back to STATIC. That
# SHARED target is dead code here (EXCLUDE_FROM_ALL — only zlibstatic is linked). The
# warning is gated on the global TARGET_SUPPORTS_SHARED_LIBS property; flip it TRUE just
# across this add_subdirectory so the SHARED target is accepted silently, then restore.
get_property(_at_tsl_save GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS)
set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS TRUE)

add_subdirectory(zlib
                 ${CMAKE_BINARY_DIR}/at_zlib_build
                 EXCLUDE_FROM_ALL)

set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS "${_at_tsl_save}")

# zlib's source trips the SDK's strict -Wundef -Wall -Wextra -Werror stack (`#if
# BIG_ENDIAN`, signedness comparisons, unused vars). Silence on the zlib target only.
target_compile_options(zlibstatic PRIVATE -w)
