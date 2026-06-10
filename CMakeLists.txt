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
add_subdirectory(component/thirdparty)

# Cross-platform LittleFS parameter set (doc/lfs.md) — geometry + runtime config
# shared by every platform's lfs port. After thirdparty so `littlefs` exists.
add_subdirectory(component/lfs-config)

# Cross-platform stdio (fd 0/1/2) console dispatcher — the shared line discipline
# + backend hook that every platform's newlib retarget routes the console fds to.
add_subdirectory(component/console)

# Cross-platform memory manager (TLSF), shared allocator across families. Bouffalo
# links the real bflb_lhal/bflb_log; other platforms use port_generic shims. After
# the chip vars are known (BOUFFALO_CHIP gates the SDK deps) but it's header/dep
# choice only, resolved at generate — safe to add here with the other shared libs.
add_subdirectory(component/bflb_mm)

# Cross-platform FreeRTOS config base — the settings common to AmebaD KM0/KM4 and
# Bouffalo; each platform's FreeRTOSConfig.h includes it (freertos_config_common.h).
add_subdirectory(component/freertos-config)

# Shared user sources (src/main.cpp + ragel-generated AT parser).
add_subdirectory(src)

# Bouffalo-specific subtree. Gated on BOUFFALO_CHIP so an AmebaD `cmake -S
# <root>` invocation (no Bouffalo toolchain) doesn't trip component/'s
# fatal-error chip check.
if(BOUFFALO_CHIP)
    add_subdirectory(component)
endif()

# AmebaD bootloader subtree. Gated on AMEBAD_CHIP (set by
# cmake/amebad-km{0,4}.cmake) so the legacy AmebaD SDK flow
# (`cmake -S <root>` with no chip toolchain, used only to produce
# user_link.list) doesn't enter the bootloader recipe. Per-module
# add_subdirectory()s live in component/amebad/CMakeLists.txt.
if(AMEBAD_CHIP MATCHES "^amebad-km[04]$")
    add_subdirectory(component/amebad)
    # NimBLE host glue (component/nimble) on top of component/amebad/hci. KM4-only
    # (the component itself early-returns for km0). Upstream is the submodule
    # component/thirdparty/nimble.
    add_subdirectory(component/nimble)
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
