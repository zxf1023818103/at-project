# bouffalo_sdk — umbrella INTERFACE library that links all chip-relevant
# Bouffalo components in one shot. Consumers do:
#
#   target_link_libraries(<exe> PRIVATE bouffalo_sdk)
#
# and pick up the low-level HAL, board init sources, linker script, and
# the -ufw_header link option transitively.

if(NOT BOUFFALO_CHIP)
    message(FATAL_ERROR "bouffalo_sdk: BOUFFALO_CHIP not set (use a chip toolchain)")
endif()

# FreeRTOS-Kernel (component/freertos-kernel, the bouffalo-port branch on
# the zxf1023818103 fork). Configure it BEFORE add_subdirectory so the
# kernel's CMakeLists can read FREERTOS_PORT + freertos_config.
if(NOT TARGET freertos_config)
    if("${BOUFFALO_CHIP}" STREQUAL "bl602")
        set(_mtime_base    "0x0200BFF8UL")
        set(_mtimecmp_base "0x02004000UL")
    else()
        # bl616, bl616cl
        set(_mtime_base    "0xE000BFF8UL")
        set(_mtimecmp_base "0xE0004000UL")
    endif()
    add_library(freertos_config INTERFACE)
    target_include_directories(freertos_config SYSTEM INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/bflb_freertos_port/include"
    )
    target_compile_definitions(freertos_config INTERFACE
        "configMTIME_BASE_ADDRESS=${_mtime_base}"
        "configMTIMECMP_BASE_ADDRESS=${_mtimecmp_base}"
        portasmHANDLE_INTERRUPT=interrupt_entry
        default_interrupt_handler=freertos_risc_v_trap_handler
    )
endif()
set(FREERTOS_PORT GCC_RISC_V CACHE STRING "FreeRTOS port name" FORCE)
if(NOT TARGET freertos_kernel)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../freertos-kernel"
        "${CMAKE_BINARY_DIR}/component/freertos-kernel"
    )
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
add_subdirectory(bflb_mm)
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
# Always built — cmake/lwip.cmake hard-requires this target, and a few
# bflb_* libs include lwipopts.h transitively. Lib is tiny when nothing
# pulls it.
add_subdirectory(bflb_lwip_port)

# Bouffalo's littlefs XIP-flash backend (lfs_xip_init + the four lfs_config
# read/prog/erase/sync callbacks). Always built so cmake/littlefs.cmake
# can find a backend; tiny when nothing links it.
add_subdirectory(bflb_littlefs_port)

# mbedtls port surface — split into config (headers) + port (runtime
# glue) + hw_acc (HACC-backed alt impls). Always built (cheap when
# nothing references them); cmake/mbedtls.cmake hard-requires config +
# port targets when wiring upstream mbedtls. hw_acc is consumed via the
# MBEDTLS_*_ALT mechanism, so its sources only get linked in if a
# consumer's call into upstream mbedcrypto pulls them in.
add_subdirectory(bflb_mbedtls_config)
add_subdirectory(bflb_mbedtls_port)
add_subdirectory(bflb_mbedtls_hw_acc)
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
