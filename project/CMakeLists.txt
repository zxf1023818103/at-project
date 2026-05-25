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
#   1. host C++ compiler builds earlephilhower/mklittlefs (vendored at
#      component/mklittlefs) against upstream littlefs at
#      component/littlefs/ — a native binary at
#      <build>/_mklittlefs_install/bin/mklittlefs;
#   2. that binary runs against project/resource/ to pack the directory
#      tree into project/lfs.bin with the canonical 4096-byte block /
#      1 MB image size (see doc/lfs.md).
#
# Why not just ship a prebuilt binary: the SDK previously shipped
# Linux-only mklfs binaries (under platform/bouffalo_sdk/tools/genlfs/);
# building mklittlefs from source removes that asymmetry — any platform
# with a working C++ compiler gets a native tool.

cmake_minimum_required(VERSION 3.19)
project(lfs_image NONE)

set(AT_PROJECT_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
include(${AT_PROJECT_ROOT}/cmake/lfs_image.cmake)

lfs_image_add(${CMAKE_CURRENT_LIST_DIR}/lfs.bin
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/resource)
