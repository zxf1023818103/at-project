# project/bouffalo — chip-toolchain-driven build for the AT firmware on
# BL602 / BL616 / BL616CL. Pulls everything from the at-project
# component/ tree; no dependency on platform/bouffalo_sdk's own CMake
# entry (find_package(bouffalo_sdk)) — that was the legacy path,
# preserved as CMakeLists.txt.legacy.
#
# Invocation expects a chip toolchain via -DCMAKE_TOOLCHAIN_FILE:
#   cmake -B build_bl602 -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/bl602-toolchain.cmake
# The toolchain file sets BOUFFALO_CHIP (bl602 / bl616 / bl616cl); the
# rest of this file branches off that.
#
# Bin pipeline: bflb_add_executable from cmake/bflb_elf_to_bin.cmake
# produces both the ELF and the flashable
# at_<chip>_whole_flash_data.bin in one go — no separate Makefile
# postbuild step needed.

cmake_minimum_required(VERSION 3.19)

# project() needs to run before we can rely on the toolchain file
# having been processed — CMake only loads CMAKE_TOOLCHAIN_FILE during
# the first project()'s compiler-probe step, so any variables the
# toolchain sets (BOUFFALO_CHIP, BOUFFALO_MARCH, …) aren't visible
# before this point.
project(at C CXX)

# BOUFFALO_CHIP is declared / validated by cmake/bouffalo_sdk.cmake (pulled
# in transitively via add_subdirectory(component) below) — no project-side
# pre-check needed.

set(AT_PROJECT_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
list(APPEND CMAKE_MODULE_PATH ${AT_PROJECT_ROOT}/cmake)

# Pull project-wide knobs out of proj.conf (chip-conditional CONFIG_*).
# Some of these are read by the bouffalo_sdk umbrella as it processes
# the subdirectory below, so they must be set first. proj.conf expects
# CHIP to identify the active chip — mirror BOUFFALO_CHIP into it.
set(CHIP "${BOUFFALO_CHIP}")
include(proj.conf)

if(NOT DEFINED PROJECT_SDK_VERSION)
    set(PROJECT_SDK_VERSION "0.0.0" CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Component tree
# ---------------------------------------------------------------------------

# component/CMakeLists.txt enumerates every subdir of component/ and
# pulls each in via its matching cmake/<name>.cmake glue. Each glue
# file uses include_guard() and pulls its own deps (e.g. cmake/lwip.cmake
# pulls cmake/bouffalo_sdk.cmake for the bflb_lwip_port headers), so a
# single add_subdirectory is enough — no need to hand-list libs here.
add_subdirectory(${AT_PROJECT_ROOT}/component
                 ${CMAKE_BINARY_DIR}/component)

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------

# User sources discovered via src/sources.cmake (file(GLOB ...)).
include(${AT_PROJECT_ROOT}/src/sources.cmake)

# bflb_add_executable creates the ELF target + a sibling custom-target
# that runs tool/bflb_elf_to_bin.py to produce
# at_<chip>_whole_flash_data.bin (and the OTA / xz / hash variants).
# Pulled in transitively by cmake/bouffalo_sdk.cmake, no explicit
# include needed.
bflb_add_executable(at ${AT_USER_SOURCES})

target_include_directories(at PRIVATE
    ${AT_USER_SRC_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(at PRIVATE bouffalo_sdk)
