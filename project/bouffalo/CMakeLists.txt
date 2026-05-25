cmake_minimum_required(VERSION 3.19)
project(at C CXX)

set(AT_PROJECT_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
list(APPEND CMAKE_MODULE_PATH ${AT_PROJECT_ROOT}/cmake)

# Pull project-wide knobs out of proj.conf (chip-conditional CONFIG_*).
# Some of these are read by the bouffalo_sdk umbrella as it processes
# the subdirectory below, so they must be set first. proj.conf reads
# BOUFFALO_CHIP directly (set by the chip toolchain file).
include(proj.conf)

# PROJECT_SDK_VERSION is declared (cache STRING) and validated by
# component/bouffalo_sdk/bflb_sdk_version/CMakeLists.txt — projects can
# override on the cmake command line with -DPROJECT_SDK_VERSION=x.y.z.

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
# that runs component/bouffalo_sdk/bflb_image_tool/bflb_elf_to_bin.py to produce
# at_<chip>_whole_flash_data.bin (and the OTA / xz / hash variants).
# Pulled in transitively by cmake/bouffalo_sdk.cmake, no explicit
# include needed.
bflb_add_executable(at ${AT_USER_SOURCES})

target_include_directories(at PRIVATE
    ${AT_USER_SRC_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(at PRIVATE bouffalo_sdk)
