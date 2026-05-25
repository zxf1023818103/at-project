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

# Default build type: Release (-O3 -DNDEBUG). Without it CMake leaves
# CMAKE_<LANG>_FLAGS_<TYPE> unset and code compiles -O0, which:
#   - balloons the BL616 boot path (.init_code in rom_header overflows
#     its 6 KB budget — boot rom_header is sized assuming -O2/-Os),
#   - and bloats the BL602 ITCM headroom that ROMAPI just won the back.
# Override on the cmake command line if you want a debug build:
#   cmake -DCMAKE_BUILD_TYPE=Debug -B build ...
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Default to optimized build" FORCE)
endif()

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
include(${AT_PROJECT_ROOT}/cmake/bflb_elf_to_bin.cmake)

bflb_add_executable(at ${AT_USER_SOURCES})

target_include_directories(at PRIVATE
    ${AT_USER_SRC_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# Stitch the project-level libs (port adapters + upstream submodule
# wrappers + Bouffalo bridges) into the bouffalo_sdk umbrella here, so
# the app below only needs `target_link_libraries(at PRIVATE bouffalo_sdk)`.
# Has to happen after all add_subdirectory / include() above — the
# umbrella subdir runs first (it defines bflb_*_port, which the
# cmake/*.cmake glue then consumes via the port's lwipopts.h /
# mbedtls_config.h), so we can only know which TARGETs exist now.
foreach(_lib IN ITEMS
        bflb_lwip_port bflb_littlefs_port bflb_mbedtls_port
        lwipcore lwip_dhcpd
        mbedtls mbedcrypto mbedx509
        cjson cjson_utils
        littlefs lfs_easyflash
        zlibstatic
        bflb_zypher_bluetooth)
    if(TARGET ${_lib})
        target_link_libraries(bouffalo_sdk INTERFACE ${_lib})
    endif()
endforeach()

# Close the linker group opened in target_link_options below. Append
# `-Wl,--end-group` as the LAST entry of bouffalo_sdk's INTERFACE link
# list so that when `at` links against bouffalo_sdk, the .a expansion
# emits the close-marker as the final token after every transitively-
# pulled archive. (Putting it directly in target_link_libraries(at ...)
# instead lands it BEFORE the INTERFACE expansion in the link line —
# wrong position, still warns.) Without it ld warns "missing
# --end-group; added as last command line option" — the group still
# closes (ld auto-appends), but the warning is noise.
target_link_libraries(bouffalo_sdk INTERFACE -Wl,--end-group)

target_link_libraries(at PRIVATE bouffalo_sdk)

# Open a linker group so all archives that follow get rescanned until
# no new undefined references resolve. The bouffalo_sdk pieces have
# cyclic references across archives (bl602_std ↔ bflb_sysinit,
# wifi4_manager ↔ wifi4_wpa_supplicant, libc nuttx-stdlib ↔ everything
# pulling memset/strlen, libfirmware.a ↔ wifi4_manager, etc.) that a
# single-pass scan can't resolve. --end-group closes via the
# bouffalo_sdk INTERFACE-link trick above.
target_link_options(at PRIVATE
    -Wl,--start-group
    -Wl,-Map=$<TARGET_FILE_BASE_NAME:at>.map
    # Force pull-in of SDK's newlib retargets (port_memory.c / syscalls_nosys.c)
    # from libbflb_libc.a. Without these -u refs, newlib's own _malloc_r wins
    # (it uses ecall SYS_brk which isn't serviced on bare-metal), causing
    # heap_end=0 and a NULL store fault inside _malloc_r on the first malloc.
    -Wl,-u,_malloc_r
    -Wl,-u,_free_r
    -Wl,-u,_calloc_r
    -Wl,-u,_realloc_r
    -Wl,-u,_memalign_r
    -Wl,-u,_sbrk_r
    -Wl,-u,_sbrk
    # The toolchain enables -flto on link, which makes gcc replay
    # compilation of every TU in the prebuilt vendor archives
    # (libwl80211_${chip}.a / libphyrf_${chip}.a — both shipped by the
    # SDK as LTO-fat .a, with jenkins-build paths baked into DWARF).
    # That replay surfaces -Wmaybe-uninitialized in upstream TUs
    # (cntrl.c / ap.c / scan.c / phy.h) we can't touch. Quiet them at
    # link only — compile-time -Wmaybe-uninitialized stays on for our
    # own sources.
    -Wno-maybe-uninitialized
)
