# amebad_sdk_includes — chip-aware INTERFACE bundle of the SDK include paths the
# AmebaD vendored headers still expect on the search path.
#
# Pruned to the LOAD-BEARING set only: the broad out-of-tree
# (platform/ameba-rtos-d) header dirs that ameba_soc.h + the os abstraction +
# the project config genuinely drag in. The dead/replaceable ones were dropped —
# WLAN driver (minus the one include/ that holds the global autoconf.h), lwip
# v1.4.1, INIC, polarssl, the SDK's mbedtls-2.4.0 (replaced with the SHARED
# component/thirdparty/mbedtls), test/example/utilities, api/wifi, api/network,
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

if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    set(_proj_cfg_inc   "${CMAKE_SOURCE_DIR}/component/amebad/app_km0")       # project config (flattened from sdk_headers/inc/inc_lp)
    # KM0 runs the upstream ARMv6-M CM0 port (matches the SDK LP port; the native
    # ARMv8-M CM23_NTZ port faults on scheduler start because it resets MSP from
    # KM0 image2's VTOR[0], which is not a valid stack top — see
    # component/amebad/freertos_port/CMakeLists.txt). Must match the kernel's port so
    # portmacro.h (pulled in by FreeRTOS.h) is consistent everywhere.
    set(_kernel_port    "GCC/ARM_CM0")
elseif("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    set(_proj_cfg_inc   "${CMAKE_SOURCE_DIR}/component/amebad/app_km4")       # project config (flattened from sdk_headers/inc/inc_hp)
    # KM4 image2 = Cortex-M33, non-secure, no TrustZone -> ARM_CM33_NTZ non-secure.
    set(_kernel_port    "GCC/ARM_CM33_NTZ/non_secure")
else()
    message(FATAL_ERROR
        "amebad_sdk_includes: AMEBAD_CHIP='${AMEBAD_CHIP}' not supported "
        "(expected amebad-km0 or amebad-km4)")
endif()

add_library(amebad_sdk_includes INTERFACE)
target_link_libraries(amebad_sdk_includes INTERFACE amebad_fwlib_headers amebad_xmodem amebad_string_rom amebad_cmsis_rom amebad_dlist amebad_platform_stdlib freertos_config_common)

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

# FreeRTOS headers come from the upstream V10.6.2 kernel
# (component/thirdparty/freertos-kernel), NOT the SDK's bundled v10.2.0 tree.
# These paths are on amebad_sdk_includes (not only the freertos_kernel target)
# because the SDK headers drag FreeRTOS in widely: ameba_soc.h -> freertos_pmu.h
# -> "FreeRTOS.h", so every TU that includes ameba_soc.h (ram_common, ram_hp,
# usrcfg, ...) needs FreeRTOS.h + portmacro.h on its path even though it doesn't
# link the kernel lib. Same new headers the kernel itself compiles against, so
# there is no version skew. The SDK's v10.2.0 Source/include + GCC/RTL8721D_*
# port dirs are intentionally gone.
set(_kernel "${CMAKE_SOURCE_DIR}/component/thirdparty/freertos-kernel")

target_include_directories(amebad_sdk_includes SYSTEM INTERFACE
    # FreeRTOSConfig.h is now the single cross-platform header in
    # component/freertos_config (carried in via the freertos_config_common link
    # above; macro-selected KM0/KM4 branch, tickless-idle off folded in). The old
    # per-core copies + the #include_next override are gone.
    # project-local chip configs (Kconfig autoconf + board opts + main.h) — owned
    # by the per-core app component (app_km{4,0}); no longer carries FreeRTOSConfig.h.
    "${_proj_cfg_inc}"
    # upstream FreeRTOS-Kernel V10.6.2 headers + the per-core port (portmacro.h)
    "${_kernel}/include"
    "${_kernel}/portable/${_kernel_port}"
    # OS abstraction headers now live with their owning components, not in the
    # sdk_headers mirror: freertos glue headers (freertos_service/pmu/heap5_config/
    # backtrace_ext.h) with freertos_port, osdep headers (osdep_service/
    # psram_reserve/device_lock/tcm_heap.h) with osdep.
    "${CMAKE_SOURCE_DIR}/component/amebad/freertos_port"
    "${CMAKE_SOURCE_DIR}/component/amebad/osdep"
    # SoC-level. monitor (shell/monitor_rom/rtl_trace.h) headers live here;
    # xmodem (xmodem_*.h) + swlib-string (strproc/memproc/printf/diag...) + CMSIS
    # (core_cm*/arm_math/mpu...) headers come via the amebad_xmodem /
    # amebad_string_rom / amebad_cmsis_rom targets linked above.
    "${CMAKE_SOURCE_DIR}/component/amebad/monitor"
    # PMU: rtl8721d_freertos_pmu.h moved to the pmu component (with lib_pmc_*.a).
    "${CMAKE_SOURCE_DIR}/component/amebad/pmu"
    # libc-retarget header — platform_stdlib_rtl8721d.h (printf/mem*/str* -> ROM,
    # malloc -> pvPortMalloc, ...) is its own header-only component now
    # (amebad_platform_stdlib, linked above), matching dlist. The old
    # platform_stdlib.h dispatcher (multi-chip #if chain, only 8721d ever taken) is
    # gone. time64.h stays in the AmebaD syscall_port dir (the newlib retarget core
    # newlib_syscalls.c is component/syscall_config), included bare as "time64.h".
    # at_cmd headers (log_service.h / atcmd_*.h) live on
    # the at_cmd component; lwip_netconf.h on lwip/config.
    "${CMAKE_SOURCE_DIR}/component/amebad/syscall_port"
    "${CMAKE_SOURCE_DIR}/component/amebad/at_cmd"
    # autoconf.h (the global Kconfig autoconf) lives in the WLAN driver's include/
    # — a Realtek quirk. Needed by the app config main.h + sys_arch.c even with WiFi off;
    # the rest of the WLAN driver headers are not pulled by the minimal build.
    # Moved out of the sdk_headers mirror onto the wlan component (with lib_wlan.a
    # / lib_wps.a). Ungated here (autoconf.h is needed on BOTH cores); the wlan
    # component itself is KM4-gated for the prebuilt libs.
    "${CMAKE_SOURCE_DIR}/component/amebad/wlan"
    # SSL — rtl8721d_ota.h (default-on HTTPS_OTA_UPDATE) pulls mbedtls/version.h
    # into every TU that includes ameba_soc.h. Take it from the SHARED upstream
    # Mbed TLS (component/thirdparty/mbedtls), not the SDK's mbedtls-2.4.0.
    "${CMAKE_SOURCE_DIR}/component/thirdparty/mbedtls/include"
)
