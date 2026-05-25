cmake_minimum_required(VERSION 3.20)
project(at_project C CXX ASM)

# Root entry for the at_project unified build. Used both:
#   - directly:  AmebaD SDK's library/Makefile runs `cmake -S <at-project-root>`
#                to produce build/amebad_user/user_link.list, which the SDK
#                Make build then cats into its final link line. In this mode
#                CMAKE_SOURCE_DIR equals this directory; component/ stays
#                inactive (no BOUFFALO_CHIP); src/ emits at_user + user_link.list.
#   - wrapped:   project/bouffalo/CMakeLists.txt does add_subdirectory(<this root>),
#                with BOUFFALO_CHIP set via the chip toolchain file. component/
#                then drives the full Bouffalo subtree (bouffalo_sdk + lwip_dhcpd +
#                lfs_easyflash + lfs_image); src/ exports AT_USER_SOURCES via
#                PARENT_SCOPE so the wrapper can feed it to bflb_add_executable().
#
# Add new top-level dirs here.

# Cross-platform third-party libs (cjson / littlefs / zlib). Always added —
# AmebaD reads zlibstatic out of the user_link.list it produces, Bouffalo
# stitches the same targets into its bouffalo_sdk umbrella below.
add_subdirectory(component/thridparty)

# Shared user sources (src/main.cpp + ragel-generated AT parser).
add_subdirectory(src)

# Bouffalo-specific subtree. Gated on BOUFFALO_CHIP so an AmebaD `cmake -S
# <root>` invocation (no Bouffalo toolchain) doesn't trip component/'s
# fatal-error chip check.
if(BOUFFALO_CHIP)
    add_subdirectory(component)
endif()

# AmebaD KM0 bootloader subtree. Gated on AMEBAD_CHIP (set by
# cmake/amebad-km0.cmake) so the legacy AmebaD SDK flow
# (`cmake -S <root>` with no chip toolchain, used only to produce
# user_link.list) doesn't enter the bootloader recipe.
if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    add_subdirectory(component/amebad/fwlib_headers)
    add_subdirectory(component/amebad/sdk_includes)
    add_subdirectory(component/amebad/ram_common)
    add_subdirectory(component/amebad/ram_lp)
    add_subdirectory(component/amebad/ram_hp)            # no-op under KM0 (self-gated)
    add_subdirectory(component/amebad/bootloader_km0)
endif()

# Forward src/'s AT_USER_SOURCES / AT_USER_SRC_DIR one more scope up so a
# per-chip project (project/bouffalo, etc.) that wraps this root via
# add_subdirectory still sees the exports. Skipped when this file is the
# top-level (AmebaD flow) — CMake would emit a dev-warning about
# PARENT_SCOPE with no parent, and the variables aren't needed there
# anyway (the AmebaD SDK reads user_link.list emitted by src/).
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_LIST_DIR)
    set(AT_USER_SOURCES ${AT_USER_SOURCES} PARENT_SCOPE)
    set(AT_USER_SRC_DIR ${AT_USER_SRC_DIR} PARENT_SCOPE)
endif()
