# Shared zlib integration fragment. Three project CMakeLists.txt
# (bouffalo / ameba-new / amebad-via-src) all include this file so the
# option knobs and add_subdirectory call live in exactly one place.
#
# Using madler/zlib v1.3.1 (the upstream reference). It uses plain `int`
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
# With EXCLUDE_FROM_ALL, only the target we link to (zlibstatic) gets
# pulled into the build, so the SHARED target is effectively dead code.

set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# madler/zlib's CMakeLists runs check_function_exists / check_c_source
# that default to a tiny executable link. Our cross toolchains need
# -nostartfiles + a custom linker script and can't link a bare exe.
# Make the compile checks fall back to STATIC_LIBRARY.
set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../thirdparty/zlib ${CMAKE_BINARY_DIR}/at_zlib_build EXCLUDE_FROM_ALL)

# zlib's source uses constructs that trip the Ameba SDK's strict
# -Wundef -Wall -Wextra -Werror flag stack (`#if BIG_ENDIAN`, signedness
# comparisons, unused vars, etc.). Silence them on the zlib target only.
if(TARGET zlibstatic)
    target_compile_options(zlibstatic PRIVATE -w)
endif()
