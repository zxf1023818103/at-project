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

cmake_minimum_required(VERSION 3.16)

# project() needs to run before we can rely on the toolchain file
# having been processed — CMake only loads CMAKE_TOOLCHAIN_FILE during
# the first project()'s compiler-probe step, so any variables the
# toolchain sets (BOUFFALO_CHIP, BOUFFALO_MARCH, …) aren't visible
# before this point.
project(at C CXX ASM)

if(NOT BOUFFALO_CHIP)
    message(FATAL_ERROR
        "BOUFFALO_CHIP not set — include a chip toolchain via "
        "-DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/bl602-toolchain.cmake "
        "(or bl616/bl616cl).")
endif()

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

# bouffalo_sdk umbrella: core HAL + std + sysinit + freertos_port +
# log/mm/sys/mtd/partition + bl<chip>_phyrf/rfparam/board_config + …
# pulled in transitively. Conditionally adds wifi4 when CONFIG_WIFI4 is
# on (BL602 only — wifi6 not extracted yet).
add_subdirectory(${AT_PROJECT_ROOT}/component/bouffalo_sdk
                 ${CMAKE_BINARY_DIR}/component/bouffalo_sdk)

# Cross-vendor library submodules (each cmake/<name>.cmake is the
# native-CMake glue that wraps the upstream submodule for our toolchain).
include(${AT_PROJECT_ROOT}/cmake/lwip.cmake)
include(${AT_PROJECT_ROOT}/cmake/mbedtls.cmake)
include(${AT_PROJECT_ROOT}/cmake/littlefs.cmake)
include(${AT_PROJECT_ROOT}/cmake/cjson.cmake)
include(${AT_PROJECT_ROOT}/cmake/zlib.cmake)

# Bouffalo-side bridges that sit at component/ top level (siblings to
# the upstream submodules they wrap).
add_subdirectory(${AT_PROJECT_ROOT}/component/lwip_dhcpd
                 ${CMAKE_BINARY_DIR}/component/lwip_dhcpd)
include(${AT_PROJECT_ROOT}/cmake/lfs_easyflash.cmake)

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------

# User sources discovered via src/sources.cmake (file(GLOB ...)).
include(${AT_PROJECT_ROOT}/src/sources.cmake)

# bflb_add_executable creates the ELF target + a sibling custom-target
# that runs tool/bflb_elf_to_bin.py to produce
# at_<chip>_whole_flash_data.bin (and the OTA / xz / hash variants).
include(${AT_PROJECT_ROOT}/cmake/bflb_elf_to_bin.cmake)

bflb_add_executable(at ${AT_USER_SOURCES})

target_include_directories(at PRIVATE
    ${AT_USER_SRC_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# Linker pulls everything the project actually consumes. bouffalo_sdk
# (INTERFACE) carries the core HAL + bl<chip>_board_config (which sets
# the linker script + -ufw_header link options). wifi4 / bflb_zypher_bluetooth
# are umbrella INTERFACEs that link their sublibs.
target_link_libraries(at PRIVATE
    bouffalo_sdk
    bflb_lwip_port
    bflb_littlefs_port
    bflb_mbedtls_port
    lwipcore
    lwip_dhcpd
    mbedtls
    mbedcrypto
    mbedx509
    cjson
    cjson_utils
    littlefs
    lfs_easyflash
    zlibstatic
)
if(TARGET wifi4)
    target_link_libraries(at PRIVATE wifi4)
endif()
if(TARGET bflb_zypher_bluetooth)
    target_link_libraries(at PRIVATE bflb_zypher_bluetooth)
endif()

# Open a linker group so all archives that follow get rescanned until
# no new undefined references resolve. The bouffalo_sdk pieces have
# cyclic references across archives (bl602_std ↔ bflb_sysinit,
# wifi4_manager ↔ wifi4_wpa_supplicant, libc nuttx-stdlib ↔ everything
# pulling memset/strlen, libfirmware.a ↔ wifi4_manager, etc.) that a
# single-pass scan can't resolve. GNU ld auto-closes the group at
# end-of-line.
#
# Note on legacy SDK alignment: the upstream Bouffalo SDK uses
# -Wl,--whole-archive around its SDK libs in extension.cmake, which
# keeps `.itcm_region` at ~4 KB instead of our ~24 KB (the LDS's
# bl602_*.o(.rodata) wildcards otherwise snowball). Reproducing that
# under CMake 3.28 requires (a) link-line de-duplication (CMP0156 only
# lands in 3.29+), (b) reconciling Bouffalo's port/bignum.c with
# upstream's library/bignum.c (both define every mbedtls_mpi_* symbol),
# and (c) feature-unification across multiple cascading paths through
# INTERFACE umbrellas. We tried it and hit (a)+(c) repeatedly even after
# clearing (b). Left as a follow-up: the ITCM region was bumped 16K→24K
# in component/bouffalo_sdk/bl602_board_config/bl602_flash.ld to absorb
# the difference.
target_link_options(at PRIVATE
    -Wl,--start-group
    -Wl,-Map=$<TARGET_FILE_BASE_NAME:at>.map
)

# Map file for debug/inspection. -Wl,-Map=…/at_bl602.map lands next to
# the ELF.
target_link_options(at PRIVATE
    -Wl,-Map=$<TARGET_FILE_BASE_NAME:at>.map
)
