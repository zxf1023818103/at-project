include_guard()

# Pulls in component/zlib (madler/zlib v1.3.1) as a static-only build.
# Called both from component/CMakeLists.txt (auto enumeration) and from
# the legacy project/ameba and project/bouffalo CMakeLists which can't
# use the root CMakeLists.txt entry. include_guard() makes both paths
# idempotent so a project that hits both never double-adds the target.
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

# AT_PROJECT_ROOT is set by the root CMakeLists.txt. Legacy include-script
# consumers (project/ameba, project/bouffalo) define it ad-hoc before
# including this file.
if(NOT DEFINED AT_PROJECT_ROOT)
    message(FATAL_ERROR "AT_PROJECT_ROOT must be set before including cmake/zlib.cmake")
endif()

add_subdirectory(${AT_PROJECT_ROOT}/component/zlib ${CMAKE_BINARY_DIR}/at_zlib_build EXCLUDE_FROM_ALL)

# zlib's source uses constructs that trip the Ameba SDK's strict
# -Wundef -Wall -Wextra -Werror flag stack (`#if BIG_ENDIAN`, signedness
# comparisons, unused vars, etc.). Silence them on the zlib target only.
if(TARGET zlibstatic)
    target_compile_options(zlibstatic PRIVATE -w)
    # Bouffalo SDK keeps its -march/-mabi/include flags on the
    # sdk_intf_lib INTERFACE target. Without linking against it zlib
    # gets the toolchain's default ABI (RV64GC) and bl616 link fails
    # with "ABI is incompatible with that of the selected emulation".
    # No-op on Ameba SDKs (the target doesn't exist there).
    if(TARGET sdk_intf_lib)
        target_link_libraries(zlibstatic PRIVATE sdk_intf_lib)
    endif()
endif()
