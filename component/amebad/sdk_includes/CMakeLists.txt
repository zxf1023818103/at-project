# amebad_sdk_includes — chip-aware INTERFACE bundle of the SDK include paths the
# AmebaD vendored headers still expect on the search path.
#
# Pruned to the LOAD-BEARING set only: the broad out-of-tree
# (platform/ameba-rtos-d) header dirs that ameba_soc.h + the os abstraction +
# the project config genuinely drag in. The dead/replaceable ones were dropped —
# WLAN driver (minus the one include/ that holds the global autoconf.h), lwip
# v1.4.1, INIC, polarssl, the SDK's mbedtls-2.4.0 (replaced with the SHARED
# component/thirdparty/mbedtls), test/example/utilities, api/wifi, api/network,
# mbed HAL (moved onto the ftl target, its only consumer). lwipopts.h moved to
# the vendored component/amebad/lwip/config.
#
# In-tree members (NOT out-of-tree): amebad_fwlib_headers (69 chip/HAL headers),
# our freertos/config override, the upstream freertos-kernel headers, shared
# mbedtls/include.
#
# What remains out-of-tree (the irreducible SDK header surface — would need
# vendoring those headers in-tree to remove): inc_lp/inc_hp (project config +
# base FreeRTOSConfig.h), os + os/freertos + os_dep, soc cmsis/monitor/xmodem/
# swlib/misc, common api/api-platform/api-at_cmd, and wlan/realtek/include
# (just for autoconf.h).
#
# Chip-gated: KM0 uses inc_lp; KM4 uses inc_hp.

if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    set(_proj_inc_dir   "inc_lp")
    # KM0 runs the upstream ARMv6-M CM0 port (matches the SDK LP port; the native
    # ARMv8-M CM23_NTZ port faults on scheduler start because it resets MSP from
    # KM0 image2's VTOR[0], which is not a valid stack top — see
    # component/amebad/freertos/CMakeLists.txt). Must match the kernel's port so
    # portmacro.h (pulled in by FreeRTOS.h) is consistent everywhere.
    set(_kernel_port    "GCC/ARM_CM0")
elseif("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    set(_proj_inc_dir   "inc_hp")
    # KM4 image2 = Cortex-M33, non-secure, no TrustZone -> ARM_CM33_NTZ non-secure.
    set(_kernel_port    "GCC/ARM_CM33_NTZ/non_secure")
else()
    message(FATAL_ERROR
        "amebad_sdk_includes: AMEBAD_CHIP='${AMEBAD_CHIP}' not supported "
        "(expected amebad-km0 or amebad-km4)")
endif()

add_library(amebad_sdk_includes INTERFACE)
target_link_libraries(amebad_sdk_includes INTERFACE amebad_fwlib_headers)

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
#     fwlib_headers/rtl8721d_ota.h provides the default via #ifndef so
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

# Now points at the IN-TREE vendored mirror (component/amebad/sdk_headers), not
# platform/ameba-rtos-d. Only the 33 headers the build actually pulls (per the
# compiler dependency files) were copied in, preserving the SDK's component/...
# layout so cross-includes resolve. Resync from the submodule with:
#   for f in <the kept include roots>; copy the used *.h ... (or re-derive from
#   the *.o.d / compiler_depend.make of a fresh build).
set(_sdk_base "${CMAKE_SOURCE_DIR}/component/amebad/sdk_headers")
set(_sdk_soc  "${_sdk_base}/component/soc/realtek/amebad")
set(_os       "${_sdk_base}/component/os")
set(_common   "${_sdk_base}/component/common")
set(_proj     "${_sdk_base}")

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
    # FreeRTOSConfig.h: our driver-only override FIRST, so its #include_next
    # chains down to the SDK base config in inc_lp/inc_hp below. Putting the
    # override here (not only on freertos_config) keeps every consumer's config
    # consistent (e.g. tickless-idle off), not just the kernel/glue/app.
    "${CMAKE_SOURCE_DIR}/component/amebad/freertos/config"
    # project-local chip configs (Kconfig autoconf + board opts + base FreeRTOSConfig.h)
    "${_proj}/inc/${_proj_inc_dir}"
    # upstream FreeRTOS-Kernel V10.6.2 headers + the per-core port (portmacro.h)
    "${_kernel}/include"
    "${_kernel}/portable/${_kernel_port}"
    # OS abstraction headers now live with their owning components, not in the
    # sdk_headers mirror: freertos glue headers (freertos_service/pmu/heap5_config/
    # backtrace_ext/cmsis_os.h) with freertos_glue, osdep headers (osdep_service/
    # psram_reserve/device_lock/tcm_heap.h) with osdep.
    "${CMAKE_SOURCE_DIR}/component/amebad/freertos_glue"
    "${CMAKE_SOURCE_DIR}/component/amebad/osdep"
    # SoC-level. monitor (shell/monitor_rom/rtl_trace.h) + xmodem (xmodem_*.h)
    # headers also moved onto their components.
    "${_sdk_soc}/cmsis"
    "${CMAKE_SOURCE_DIR}/component/amebad/monitor"
    "${CMAKE_SOURCE_DIR}/component/amebad/xmodem"
    "${_sdk_soc}/swlib/string"
    "${_sdk_soc}/misc"
    # common APIs — platform_stdlib.h etc. (api + api/platform). at_cmd headers
    # (log_service.h / atcmd_*.h) moved onto the at_cmd component; lwip_netconf.h
    # moved to lwip/config.
    "${_common}/api"
    "${_common}/api/platform"
    "${CMAKE_SOURCE_DIR}/component/amebad/at_cmd"
    # autoconf.h (the global Kconfig autoconf) lives in the WLAN driver's include/
    # — a Realtek quirk. Needed by inc_lp/main.h + sys_arch.c even with WiFi off;
    # the rest of the WLAN driver headers are not pulled by the minimal build.
    "${_common}/drivers/wlan/realtek/include"
    # SSL — rtl8721d_ota.h (default-on HTTPS_OTA_UPDATE) pulls mbedtls/version.h
    # into every TU that includes ameba_soc.h. Take it from the SHARED upstream
    # Mbed TLS (component/thirdparty/mbedtls), not the SDK's mbedtls-2.4.0.
    "${CMAKE_SOURCE_DIR}/component/thirdparty/mbedtls/include"
)
