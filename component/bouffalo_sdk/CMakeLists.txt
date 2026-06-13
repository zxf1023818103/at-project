# bouffalo_sdk — umbrella INTERFACE library that links all chip-relevant
# Bouffalo components in one shot. Consumers do:
#
#   target_link_libraries(<exe> PRIVATE bouffalo_sdk)
#
# and pick up the low-level HAL, board init sources, linker script, and
# the -ufw_header link option transitively.

# BOUFFALO_CHIP gating happens in component/CMakeLists.txt (the file
# that add_subdirectory's this one). We can rely on it being set + valid
# by the time we get here.

# FreeRTOS-Kernel (component/freertos-kernel_config/freertos-kernel, the bouffalo-port
# branch on the zxf1023818103 fork). Configure it BEFORE add_subdirectory
# so the kernel's CMakeLists can read FREERTOS_PORT + freertos_config.
#
# `freertos_config` is the cross-platform STATIC at the repo root
# (component/freertos-kernel_config), which now also derives FREERTOS_PORT (GCC_RISC_V for
# BOUFFALO_CHIP) + FREERTOS_HEAP and add_subdirectory's the kernel itself — so freertos_kernel
# / FreeRTOS already exist here. Bouffalo's RISC-V-specific config (atomic_rtos.h path + mtime
# / trap-handler defs + the kernel-header feed) is attached to freertos_config from the
# bflb_freertos_port COMPONENT; it resolves at generate time, so it still reaches the kernel's
# compile.
if(NOT TARGET freertos_kernel)
    message(FATAL_ERROR
        "freertos_kernel target missing — component/freertos-kernel_config must be added "
        "(repo-root CMakeLists) before component (bouffalo_sdk)")
endif()

# bflb_autoconf must come first: its INTERFACE forces -include autoconf.h
# into every TU that links it (transitively, via bflb_lhal below).
add_subdirectory(bflb_autoconf)
add_subdirectory(bflb_freertos_port)
add_subdirectory(bflb_sdk_version)
add_subdirectory(bflb_sysinit)
# std must be added before lhal — lhal links against ${BOUFFALO_CHIP}_std.
add_subdirectory(${BOUFFALO_CHIP}_std)
add_subdirectory(bflb_lhal)
add_subdirectory(bflb_log)
# bflb_mm moved to the shared top-level component (component/bflb_mm), added by the
# repo-root CMakeLists before this subtree; the `bflb_mm` target already exists.
add_subdirectory(bflb_block_pool)
add_subdirectory(bflb_timestamp)
add_subdirectory(bflb_partition)
add_subdirectory(bflb_mtd)
add_subdirectory(bflb_sys)
add_subdirectory(bflb_async_event)
# bflb_power_mgmt is a BL616+ thing (clock_manager + pm_manager + tickless
# stack built around the LP-fw framework). BL602 predates all of this —
# its bl_lp.h has no chip-specific impl, several SDK symbols
# (iot2lp_para, HBN_*) don't exist on BL602, and bouffalo's BL602 power-
# mgmt path goes through wifi_hosal's own bflb_pm.c. Skip the lib on
# BL602 to avoid pulling in code that won't compile.
if(NOT "${BOUFFALO_CHIP}" STREQUAL "bl602")
    add_subdirectory(bflb_power_mgmt)
endif()
# Bouffalo's lwIP port layer (arch + lwipopts.h + ethernetif + sys_arch).
# Always built — the lwip block in component/thirdparty/CMakeLists.txt
# hard-requires this target, and a few
# bflb_* libs include lwipopts.h transitively. Lib is tiny when nothing
# pulls it.
add_subdirectory(bflb_lwip_port)

# Bouffalo's littlefs XIP-flash backend (lfs_xip_init + the four lfs_config
# read/prog/erase/sync callbacks). Always built so consumers of the
# `littlefs` target from component/thirdparty/ find a backend; tiny
# when nothing links it.
add_subdirectory(bflb_littlefs_port)

# mbedtls port surface — split into config (headers) + port (runtime
# glue) + hw_acc (HACC-backed alt impls). All three are prereqs for the
# upstream mbedtls pull immediately below, so they're added first. hw_acc
# is consumed via the MBEDTLS_*_ALT mechanism, so its sources only get
# linked in if a consumer's call into upstream mbedcrypto pulls them in.
add_subdirectory(bflb_mbedtls_config)
add_subdirectory(bflb_mbedtls_port)
add_subdirectory(bflb_mbedtls_hw_acc)

# -------------------------------------------------------------------------
# Upstream Mbed TLS v3.6.4 (component/mbedtls_config/mbedtls, clean upstream).
# Pulled here — not in component/thirdparty/CMakeLists.txt — because the
# upstream sources are unbuildable without bflb_mbedtls_config /
# _port / _hw_acc headers, and nothing outside the Bouffalo SDK consumes
# upstream mbedtls. Keeps the dependency one-directional.
#
# Why v3.6.4 and not v3.6.5+: the 3.6.5 release reorganized the source
# tree (most primitives moved under tf-psa-crypto/drivers/builtin/src/)
# AND removed the classic MBEDTLS_*_ALT mechanism for the in-tree builtin
# drivers. Bouffalo's HW-accelerated alt impls (aes_alt.c, sha*_alt.c,
# bignum_alt.c, ecp_alt.c, …) rely on MBEDTLS_*_ALT to substitute the
# software implementations at compile time. Pinning at 3.6.4 keeps that
# contract intact while still being a supported LTS release.
#
# Targets produced (all STATIC — BUILD_SHARED_LIBS forced OFF):
#   mbedcrypto    crypto primitives + PSA + DRBG
#   mbedx509      X.509 cert handling
#   mbedtls       TLS protocol (depends on the above)
#   everest       Curve25519 fast impl (3rdparty submodule)
#   p256m         secp256r1 fast impl (3rdparty submodule)

# Strip upstream's testing scaffolding and install/export — none of it
# makes sense on bare metal.
set(ENABLE_TESTING       OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS      OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS    OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON  CACHE BOOL "" FORCE)
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
set(GEN_FILES            OFF CACHE BOOL "" FORCE)

# Point upstream at the Bouffalo config wrapper (config-tls-generic.h)
# carried by bflb_mbedtls_config. Use -DMBEDTLS_CONFIG_FILE +
# include_directories BEFORE the subdirectory so every target the
# upstream tree creates (mbedcrypto, mbedtls, mbedx509, p256m, everest,
# …) sees both the macro AND the path that makes it resolve.
add_compile_definitions(MBEDTLS_CONFIG_FILE="config-tls-generic.h")
include_directories("${CMAKE_CURRENT_LIST_DIR}/bflb_mbedtls_config")
# hw_acc headers (aes_alt.h etc.) likewise need to be on the include
# path of every TU because upstream's public headers #include "aes_alt.h"
# unconditionally when MBEDTLS_AES_ALT is set.
include_directories("${CMAKE_CURRENT_LIST_DIR}/bflb_mbedtls_hw_acc")
# Same story for the port header named by MBEDTLS_PLATFORM_STD_MEM_HDR.
include_directories("${CMAKE_CURRENT_LIST_DIR}/bflb_mbedtls_port")

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../mbedtls_config/mbedtls"
                 ${CMAKE_BINARY_DIR}/at_mbedtls_build
                 EXCLUDE_FROM_ALL)

# Wire the Bouffalo include paths (config + hw_acc) onto the upstream
# targets so the config file resolves and the alt headers are visible
# when MBEDTLS_*_ALT macros are on.
#
# bflb_mbedtls_port is NOT linked here — that would create a cycle
# (port links mbedcrypto for upstream headers; doing the reverse would
# break configuration). Instead we expose only port's source directory
# to mbedcrypto (header edge, not symbol edge), so
# MBEDTLS_PLATFORM_STD_MEM_HDR="mbedtls_port_bouffalo_sdk.h" resolves
# during upstream compilation. Using
# $<TARGET_PROPERTY:bflb_mbedtls_port,SOURCE_DIR> rather than its
# INTERFACE_INCLUDE_DIRECTORIES is deliberate: port itself reads
# mbedcrypto's INTERFACE_INCLUDE_DIRECTORIES (for the mbedtls/ public
# headers), and feeding port's INTERFACE includes back into mbedcrypto
# would close a generator-expression cycle. SOURCE_DIR is a fixed
# string, so the propagation stays one-way.
foreach(_t mbedcrypto mbedtls mbedx509)
    target_link_libraries(${_t} PUBLIC bflb_mbedtls_config)
    target_include_directories(${_t} PUBLIC
        "$<TARGET_PROPERTY:bflb_mbedtls_hw_acc,SOURCE_DIR>"
        "$<TARGET_PROPERTY:bflb_mbedtls_port,SOURCE_DIR>"
    )
    target_compile_options(${_t} PRIVATE -w)
endforeach()
# Utility libs ported one-by-one from
# platform/bouffalo_sdk/components/utils/. All are header-only INTERFACEs
# or small STATIC libs; pull them all in unconditionally — they're tiny
# and consumers opt-in via target_link_libraries.
add_subdirectory(bflb_libc)
add_subdirectory(bflb_getopt)
add_subdirectory(bflb_list)
add_subdirectory(bflb_queue)
add_subdirectory(bflb_tree)
add_subdirectory(bflb_ring_buffer)
add_subdirectory(bflb_math)
add_subdirectory(bflb_coredump)
add_subdirectory(${BOUFFALO_CHIP}_board_config)

# ELF -> flashable BIN pipeline (the bflb_add_executable() helper).
# Lives next to the rest of the SDK so consumers automatically get the
# helper after linking bouffalo_sdk — no separate include() needed.
add_subdirectory(bflb_image_tool)

# phyrf only matters when RF/Wi-Fi is enabled. rfparam (BL602-only in this
# fork — BL616 / BL616CL use a different RF parameter pipeline shipped inside
# their respective phyrf .a archives) needs phyrf's headers, so it gates on
# the same flag.
if(CONFIG_RF)
    add_subdirectory(${BOUFFALO_CHIP}_phyrf)
    if("${BOUFFALO_CHIP}" STREQUAL "bl602")
        add_subdirectory(bl602_rfparam)
    endif()
endif()

# wifi4 — Wi-Fi 4 stack. BL602-only (BL616 / BL616CL use wifi6 via the
# wl80211 + macsw + bl_wpa_supplicant trio below). The umbrella here just
# pulls the subtree in; the wifi4 dir's own CMakeLists adds the six
# sublibs and defines the `wifi4` INTERFACE roll-up.
if(CONFIG_WIFI4 AND "${BOUFFALO_CHIP}" STREQUAL "bl602")
    add_subdirectory(wifi4)
endif()

# Wi-Fi 6 stack on BL616 / BL616CL: three coupled components.
#   wl80211 — Bouffalo's 802.11 driver + wifi_mgmr API surface.
#   macsw   — MAC software layer (prebuilt .a per chip × variant +
#             macsw_plat.c shim).
#   bl_wpa_supplicant — Bouffalo port of wpa_supplicant.
# CONFIG_WIFI6 is the umbrella switch in proj.conf (matches upstream).
# CONFIG_WL80211 + CONFIG_BL_WPA_SUPPLICANT are individually gated to
# mirror upstream's Kconfig but in practice on this fork the three move
# together; we wire them inside the same CONFIG_WIFI6 block.
if(CONFIG_WIFI6 AND ("${BOUFFALO_CHIP}" STREQUAL "bl616" OR
                    "${BOUFFALO_CHIP}" STREQUAL "bl616cl"))
    add_subdirectory(macsw)
    add_subdirectory(bl_wpa_supplicant)
    if(CONFIG_WL80211)
        add_subdirectory(wl80211)
    endif()
endif()

# Generic RF parameter loader for non-BL602 chips (BL602 uses its own
# bl602_rfparam component, wired above). Gated on CONFIG_RF + chip.
if(CONFIG_RF AND NOT ("${BOUFFALO_CHIP}" STREQUAL "bl602"))
    add_subdirectory(rfparam)
endif()

# bflb_zypher_bluetooth — Bouffalo's Zephyr-derived BLE host + the
# chip-specific (prebuilt) controller .a. Gated on CONFIG_BLUETOOTH;
# currently wired for BL602 (m1s1 controller profile) only — BL616 /
# BL616CL prebuilts are present but the profile-config defs aren't
# translated yet.
if(CONFIG_BLUETOOTH)
    add_subdirectory(bflb_zypher_bluetooth)
endif()

add_library(bouffalo_sdk INTERFACE)
target_link_libraries(bouffalo_sdk INTERFACE
    bflb_autoconf
    bflb_sdk_version
    bflb_freertos_port
    ${BOUFFALO_CHIP}_std
    bflb_lhal
    bflb_libc
    bflb_mm
    bflb_log
    bflb_block_pool
    bflb_timestamp
    bflb_partition
    bflb_mtd
    bflb_sys
    bflb_async_event
    ${BOUFFALO_CHIP}_board_config
    # bflb_sysinit comes last so its archive is scanned AFTER bl<chip>_std,
    # which is the only thing that references sysinit_run_all.
    bflb_sysinit
)

# Optional: BL616+ power management (excluded on BL602 — see above).
if(TARGET bflb_power_mgmt)
    target_link_libraries(bouffalo_sdk INTERFACE bflb_power_mgmt)
endif()

# OBJECT libraries don't propagate their object files transitively through an
# INTERFACE intermediary — CMake only auto-extracts $<TARGET_OBJECTS:...> when
# the OBJECT lib is linked directly to a real target. Stitching them in
# explicitly here so symbols kept alive only by linker-script KEEP() rules
# (.fw_header, .version.sdk) actually reach the final ELF.
target_sources(bouffalo_sdk INTERFACE
    $<TARGET_OBJECTS:bflb_sdk_version>
    $<TARGET_OBJECTS:${BOUFFALO_CHIP}_board_config>
)
if(CONFIG_RF)
    target_link_libraries(bouffalo_sdk INTERFACE ${BOUFFALO_CHIP}_phyrf)
    if("${BOUFFALO_CHIP}" STREQUAL "bl602")
        target_link_libraries(bouffalo_sdk INTERFACE bl602_rfparam)
    endif()
endif()

if(CONFIG_WIFI4 AND "${BOUFFALO_CHIP}" STREQUAL "bl602")
    target_link_libraries(bouffalo_sdk INTERFACE wifi4)
endif()

if(CONFIG_WIFI6 AND ("${BOUFFALO_CHIP}" STREQUAL "bl616" OR
                    "${BOUFFALO_CHIP}" STREQUAL "bl616cl"))
    target_link_libraries(bouffalo_sdk INTERFACE
        macsw
        bl_wpa_supplicant
    )
    if(TARGET wl80211)
        target_link_libraries(bouffalo_sdk INTERFACE wl80211)
    endif()
endif()

if(TARGET rfparam)
    target_link_libraries(bouffalo_sdk INTERFACE rfparam)
endif()

# Port adapter libs that always get built (bflb_lwip_port / bflb_littlefs_port
# / bflb_mbedtls_port carry headers downstream consumers transitively need
# even when nothing references the symbols). Plus the Zephyr-derived BLE host
# when CONFIG_BLUETOOTH is on. Stitching them in the umbrella here so that
# downstream projects don't have to know which sub-targets exist — they just
# do `target_link_libraries(<exe> PRIVATE bouffalo_sdk)`.
target_link_libraries(bouffalo_sdk INTERFACE
    bflb_lwip_port
    bflb_littlefs_port
    bflb_mbedtls_port
)
if(TARGET bflb_zypher_bluetooth)
    target_link_libraries(bouffalo_sdk INTERFACE bflb_zypher_bluetooth)
endif()

# Cross-platform third-party libs from component/thirdparty/ (built by its
# own CMakeLists, which knows nothing about Bouffalo) + the upstream lwip
# and mbedtls targets pulled in by the bflb_lwip_port and upstream-mbedtls
# blocks above. Sticking them on the umbrella here keeps the dependency
# one-directional: third-party doesn't peek at bouffalo_sdk.
#
# mbedcrypto / mbedtls / mbedx509 + bflb_mbedtls_hw_acc: hw_acc used to
# PUBLIC-link into mbedcrypto directly, but that closed an include-cycle
# once hw_acc started pulling mbedcrypto's INTERFACE_INCLUDE_DIRECTORIES.
# Routing both through the umbrella keeps symbol resolution intact without
# the cycle.
target_link_libraries(bouffalo_sdk INTERFACE
    zlibstatic
    cjson
    cjson_utils
    littlefs
    lwipcore
    mbedtls
    mbedcrypto
    mbedx509
    bflb_mbedtls_hw_acc
)

# Bouffalo SDK keeps its -march/-mabi/include flags on the sdk_intf_lib
# INTERFACE target. Without linking against it zlib gets the toolchain's
# default ABI (RV64GC) and bl616 link fails with "ABI is incompatible
# with that of the selected emulation". Wired here, not on zlibstatic's
# own block in thirdparty/, because sdk_intf_lib is a Bouffalo-only
# target — keeping the dependency one-directional.
if(TARGET sdk_intf_lib)
    target_link_libraries(zlibstatic PRIVATE sdk_intf_lib)
endif()

# Open a linker group around the umbrella's transitive archives + force-pull
# SDK's newlib retargets. Closing --end-group is deferred to project scope
# by component/CMakeLists.txt's _bouffalo_sdk_close_link_group() — that's
# the only way to guarantee the `-Wl,--end-group` token is appended past
# every external lib (cjson / lwip / mbedtls / …) the umbrella links
# above, since `target_link_options` emits before `target_link_libraries`
# in the final link line.
#
# Reasons for each piece:
#   --start-group: bouffalo_sdk has cyclic refs across archives
#       (bl<chip>_std <-> bflb_sysinit, wifi_manager <-> wpa_supplicant,
#        libc nuttx-stdlib <-> everything pulling memset/strlen,
#        libfirmware.a <-> wifi_manager, …) that single-pass ld can't
#       resolve.
#   -u,_malloc_r etc.: SDK's port_memory.c retargets newlib's _malloc_r /
#       _sbrk_r / friends to kmalloc() (mm subsystem), but it ships as an
#       archive member with no consumer-side reference. Without these -u
#       refs newlib's own _malloc_r (which goes through `ecall SYS_brk`,
#       unserviced on bare metal) wins -> heap_end=0 -> first malloc faults.
#   -Wno-maybe-uninitialized: the toolchain enables -flto on link, so gcc
#       replays compilation of every TU inside the prebuilt vendor archives
#       (libwl80211_*.a / libphyrf_*.a — shipped LTO-fat with jenkins-build
#       paths baked into DWARF). That replay surfaces upstream warnings
#       (cntrl.c / ap.c / scan.c / phy.h) we can't touch. Compile-time
#       -Wmaybe-uninitialized stays on for the project's own sources.
target_link_options(bouffalo_sdk INTERFACE
    -Wl,--start-group
    -Wl,-u,_malloc_r
    -Wl,-u,_free_r
    -Wl,-u,_calloc_r
    -Wl,-u,_realloc_r
    -Wl,-u,_memalign_r
    -Wl,-u,_sbrk_r
    -Wl,-u,_sbrk
    -Wno-maybe-uninitialized
)

# Close the --start-group above. `-Wl,--end-group` is smuggled in as a library entry
# (not a link option) so CMake appends it to INTERFACE_LINK_LIBRARIES — emitted AFTER
# the link-options block and after every archive the umbrella transitively pulls. The
# cmake_language(DEFER) to project scope guarantees it is the very LAST entry, past
# every external lib (cjson / lwip / mbedtls / the late lwip_dhcpd + lfs_easyflash
# self-registrations). Lives here with --start-group (was in component/CMakeLists.txt).
function(_bouffalo_sdk_close_link_group)
    target_link_libraries(bouffalo_sdk INTERFACE -Wl,--end-group)
endfunction()
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR}
    CALL _bouffalo_sdk_close_link_group)
