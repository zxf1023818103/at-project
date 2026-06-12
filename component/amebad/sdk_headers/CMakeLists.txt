# amebad_sdk_includes — chip-aware INTERFACE bundle of the SDK include paths the
# AmebaD vendored headers still expect on the search path.
#
# Pruned to the LOAD-BEARING set only: the broad out-of-tree
# (platform/ameba-rtos-d) header dirs that ameba_soc.h + the os abstraction +
# the project config genuinely drag in. The dead/replaceable ones were dropped —
# WLAN driver (minus the one include/ that holds the global autoconf.h), lwip
# v1.4.1, INIC, polarssl, the SDK's mbedtls-2.4.0 (replaced with the SHARED
# component/mbedtls_config/mbedtls), test/example/utilities, api/wifi, api/network,
# mbed HAL (moved onto the wifi target, its only consumer). lwip is fully removed
# (IP stack = FreeRTOS-Plus-TCP); the residual driver/netif ABI shim is
# component/amebad/freertos_tcp_port/netif_shim.h (target amebad_netif_shim).
#
# In-tree members (NOT out-of-tree): amebad_fwlib_headers (69 chip/HAL headers),
# our freertos/config override, the upstream freertos-kernel headers, shared
# mbedtls/include. (The old platform/ header bundle that lived here is gone:
# platform_stdlib_rtl8721d.h + time64.h moved to the syscalls component, dlist.h
# became the amebad_dlist component, and the platform_stdlib.h dispatcher was
# deleted — only 8721d was ever selected.)
#
# What remains out-of-tree (the irreducible SDK header surface — would need
# vendoring those headers in-tree to remove): os + os/freertos + os_dep, soc
# cmsis/monitor/xmodem/swlib/misc, and the wlan component's headers (just for
# autoconf.h; lib_wlan.a/lib_wps.a live there).
#
# The per-core project config (base FreeRTOSConfig.h + platform_opts/autoconf +
# main.h) is now in-tree, owned by the app component: app_km0 (KM0) /
# app_km4 (KM4). Chip-gated below via _proj_cfg_inc.

add_library(amebad_sdk_includes INTERFACE)
# The entire SDK header surface comes via each owner's header INTERFACE target —
# header deps through target_link_libraries, NOT a pile of raw
# target_include_directories. Each *_headers INTERFACE is defined before its
# component's chip gate, so it exists on both cores (the headers are needed KM0+KM4
# even where the component's compiled lib is KM4-only). The per-core port / project
# config that used to be computed here now lives with its owner (amebad_freertos_headers
# picks the kernel port path per core; amebad_proj_config is defined by the matching app).
target_link_libraries(amebad_sdk_includes INTERFACE
    # in-tree header-only components (own + expose their own dir)
    amebad_fwlib_headers
    amebad_xmodem
    amebad_string_rom
    amebad_cmsis_rom
    amebad_dlist
    amebad_platform_stdlib
    freertos_config_common
    # SDK header surface (replaces the old raw target_include_directories block):
    amebad_freertos_headers       # upstream FreeRTOS.h + portmacro.h + the freertos glue headers
    amebad_proj_config            # per-core platform_opts / Kconfig autoconf / main.h
    amebad_osdep_headers          # os + os/freertos + os_dep
    amebad_monitor_headers        # shell / monitor_rom / rtl_trace
    amebad_pmu_headers            # rtl8721d_freertos_pmu.h (pulled by ameba_soc.h)
    amebad_syscall_port_headers   # time64.h
    amebad_at_cmd_headers         # log_service.h / atcmd_*.h
    amebad_wlan_headers           # autoconf.h (global Kconfig)
    amebad_mbedtls_headers        # shared upstream mbedtls/version.h
)

# OTA region — base addresses for the two image2 partitions. Default
# values match SDK upstream (rtl8721d_ota.h #define defaults). Override
# on the cmake line when relocating image2:
#   cmake -DAMEBAD_OTA1_ADDR=0x080xx000 -DAMEBAD_OTA2_ADDR=0x081xx000 ...
#
# Notes:
#   - 4 KB aligned (RSIP mask granularity).
#   - KM0 and KM4 image2 are combined into one blob; only KM0's start
#     address needs setting (KM4 follows immediately after).
#   - Consumed everywhere via SDK macro LS_IMG2_OTA{1,2}_ADDR:
#       * boot stage:   usrcfg_common/src/rtl8721d_bootcfg.c (OTA_Region[])
#       * image2 stage: SDK misc/rtl8721d_ota.c (OtaTargetHdr lookup, etc.)
#     fwlib/rtl8721d_ota.h provides the default via #ifndef so
#     the CMake -D injection below cleanly overrides it (no -Wmacro-redefined).
#
# Lives in sdk_includes (not usrcfg_common) because the macro reaches
# beyond board config — image2-stage SDK code references the same macro.
set(AMEBAD_OTA1_ADDR "0x08006000" CACHE STRING
    "AmebaD KM0 IMG2 OTA1 partition start address (4K-aligned)")
set(AMEBAD_OTA2_ADDR "0x08183000" CACHE STRING
    "AmebaD KM0 IMG2 OTA2 partition start address (4K-aligned)")
target_compile_definitions(amebad_sdk_includes INTERFACE
    "LS_IMG2_OTA1_ADDR=${AMEBAD_OTA1_ADDR}"
    "LS_IMG2_OTA2_ADDR=${AMEBAD_OTA2_ADDR}"
)

# RSIP Mask entry 0 — covers the system-data region (BACKUP + XIPSYS,
# 2*4K starting at 0x08002000 by default). SDK comment: "Entry 0 is
# already used by System Data". Entries 1-3 are user-customizable
# directly in usrcfg_common/src/rtl8721d_bootcfg.c (no cmake option
# for those, they're meant to be hand-edited).
#
# Defaults match SDK upstream (bootcfg.c literal + layout.ld
# BACKUP(0x08002000)+XIPSYS(0x08003000)). Override on the cmake line
# when relocating system data:
#   cmake -DAMEBAD_RSIP_MASK_ENTRY0_ADDR=0xXX \
#         -DAMEBAD_RSIP_MASK_ENTRY0_PAGES=N ...
#
# Notes:
#   - ADDR: 4K aligned. PAGES: count of 4K pages, max 255.
#   - Consumed by usrcfg_common/src/rtl8721d_bootcfg.c RSIP_Mask_Config[0].
#   - If you relocate ADDR, you also need to move BACKUP/XIPSYS in the
#     layout.ld files (still hard-coded — see Wave 2 plan).
#   - Lives in sdk_includes (not usrcfg_common) for parity with OTA
#     option; any future layout-driven option goes here too.
set(AMEBAD_RSIP_MASK_ENTRY0_ADDR "0x08002000" CACHE STRING
    "AmebaD RSIP mask entry 0 start address (system data, 4K-aligned)")
set(AMEBAD_RSIP_MASK_ENTRY0_PAGES "2" CACHE STRING
    "AmebaD RSIP mask entry 0 size in 4K pages (max 255)")
target_compile_definitions(amebad_sdk_includes INTERFACE
    "AMEBAD_RSIP_MASK_ENTRY0_ADDR=${AMEBAD_RSIP_MASK_ENTRY0_ADDR}"
    "AMEBAD_RSIP_MASK_ENTRY0_PAGES=${AMEBAD_RSIP_MASK_ENTRY0_PAGES}"
)

# (The former raw target_include_directories block — kernel include/port, freertos
# glue, osdep, monitor, pmu, syscall_port, at_cmd, wlan, mbedtls — moved to each
# owner's *_headers INTERFACE, linked above. See git history for the path list.)
