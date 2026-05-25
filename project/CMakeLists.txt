# Top-level CMake entry for producing project/lfs.bin — the
# cross-platform LittleFS image that every Bouffalo / Realtek chip
# embeds at the same flash offset (0x300000 on the standard partition
# layout shared between platforms).
#
# Usage:
#   cmake -S project -B project/.lfs_build
#   cmake --build project/.lfs_build --target lfs_image
#
# project/Makefile drives this for the existing `make -C project ...`
# invocations.
#
# Producing the image:
#   1. host C compiler builds Bouffalo's tools/genlfs/mklfs.c (which
#      embeds upstream littlefs's lfs.c + lfs_util.c) into a native
#      mklfs binary at <build>/mklfs;
#   2. that binary runs against project/resource/ to pack the directory
#      tree into project/lfs.bin with the canonical 4096/256/256/1MB
#      LFS parameters (see doc/lfs.md).
#
# Why not just ship a prebuilt mklfs: the SDK ships Linux-only
# (mklfs-ubuntu) plus a macOS x86_64 binary that doesn't run on Apple
# Silicon natively; building from source removes that asymmetry —
# anyone with a working C compiler gets a native tool.

cmake_minimum_required(VERSION 3.19)
project(lfs_image NONE)

set(AT_PROJECT_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
include(${AT_PROJECT_ROOT}/cmake/lfs_image.cmake)

lfs_image_add(${CMAKE_CURRENT_LIST_DIR}/lfs.bin
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/resource)
