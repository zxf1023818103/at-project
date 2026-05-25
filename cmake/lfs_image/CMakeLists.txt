# Host-toolchain sub-project that produces the `mklfs` LittleFS image
# builder. Invoked via ExternalProject_Add() from cmake/lfs_image.cmake
# so it stays in a separate cache from the outer cross-compile build
# (CMAKE_TOOLCHAIN_FILE for the outer is intentionally NOT propagated).
#
# Inputs (CMake args from the outer ExternalProject_Add):
#   MKLFS_SOURCE_DIR    — directory containing mklfs.c (Bouffalo's
#                          tools/genlfs source).
#   LITTLEFS_SOURCE_DIR — upstream littlefs root containing
#                          lfs.c / lfs_util.c / lfs.h.
#
# Output: mklfs (or mklfs.exe on Windows) installed into the prefix
# the outer build passes via -DCMAKE_INSTALL_PREFIX.

cmake_minimum_required(VERSION 3.19)
project(mklfs C)

if(NOT MKLFS_SOURCE_DIR)
    message(FATAL_ERROR "MKLFS_SOURCE_DIR must be set (the directory containing mklfs.c)")
endif()
if(NOT LITTLEFS_SOURCE_DIR)
    message(FATAL_ERROR "LITTLEFS_SOURCE_DIR must be set (upstream littlefs root)")
endif()

add_executable(mklfs
    ${MKLFS_SOURCE_DIR}/mklfs.c
    ${LITTLEFS_SOURCE_DIR}/lfs.c
    ${LITTLEFS_SOURCE_DIR}/lfs_util.c
)
target_include_directories(mklfs PRIVATE ${LITTLEFS_SOURCE_DIR})
# Upstream littlefs + mklfs.c trigger a few benign warnings under -Wall
# (printf format mismatches, unused result of read()). Silence them — the
# upstream sources are pinned at known revisions, we don't audit them.
target_compile_options(mklfs PRIVATE -O2 -Wall -Wno-format -Wno-unused-result)

install(TARGETS mklfs RUNTIME DESTINATION bin)
