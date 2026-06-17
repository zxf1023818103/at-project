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

# Cross-platform third-party libs. Each lib now lives in its own
# component/<name>_config (submodule + build wrapper); the universal ones are
# assembled here. AmebaD reads zlibstatic out of the user_link.list it produces,
# Bouffalo stitches the same targets into its bouffalo_sdk umbrella below.
# (The consumer-coupled libs — mbedtls/lwip/freertos-*/tfm — are added from their
# consumers, not here.)
add_subdirectory(component/zlib_config)
add_subdirectory(component/cjson_config)
# littlefs_config: the littlefs submodule (-> `littlefs`) PLUS the cross-platform
# LittleFS service layer (-> `lfs_config`: geometry + mount lifecycle, doc/lfs.md).
add_subdirectory(component/littlefs_config)

# Cross-platform stdio (fd 0/1/2) console dispatcher — the shared line discipline
# + backend hook that every platform's newlib retarget routes the console fds to.
add_subdirectory(component/console)

# Cross-platform memory manager (TLSF), shared allocator across families. Bouffalo
# links the real bflb_lhal/bflb_log; other platforms use port_generic shims. After
# the chip vars are known (BOUFFALO_CHIP gates the SDK deps) but it's header/dep
# choice only, resolved at generate — safe to add here with the other shared libs.
add_subdirectory(component/tlsf_config)

# Cross-platform FreeRTOS config base — the settings common to AmebaD KM0/KM4 and
# Bouffalo; each platform's FreeRTOSConfig.h includes it (freertos_config_common.h).
add_subdirectory(component/freertos-kernel_config)

# Cross-platform FreeRTOS-Plus-POSIX layer (pthread/mqueue/sem/timer over the kernel).
# After freertos-kernel_config so the `FreeRTOS` alias exists. EXCLUDE_FROM_ALL — built
# only when a consumer links freertos_posix / freertos-posix_config.
add_subdirectory(component/freertos-posix_config)

# Shared user sources (src/main.cpp + ragel-generated AT parser).
add_subdirectory(src)

# Platform component subtree. Entered for whichever platform's chip vars are set:
# BOUFFALO_CHIP (Bouffalo SDK subtree) or AMEBAD_CHIP (the AmebaD config dirs +
# bootloader/app subtree) — component/CMakeLists.txt branches internally and owns
# each platform's children, so this stays a single strict add_subdirectory. Neither
# is set in the legacy AmebaD SDK flow (`cmake -S <root>` with no chip toolchain, used
# only to emit user_link.list), so component/ correctly stays inactive there.
if(BOUFFALO_CHIP OR AMEBAD_CHIP MATCHES "^amebad-km[04]$")
    add_subdirectory(component)
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
