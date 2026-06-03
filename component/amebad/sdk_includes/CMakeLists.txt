# amebad_sdk_includes — chip-aware INTERFACE bundle of all SDK include
# paths that AmebaD vendor headers expect on the search path.
#
# Mirrors IFLAGS from
#   platform/ameba-rtos-d/.../project_lp/asdk/Makefile.include.gen (lines 146-211).
# SDK MISS paths (cmsis/device, fwlib/rom_map_lp, wifi_fw/include,
# swlib/os_dep/*, swlib/std_lib/*, mbed/.../include, $(ROOTDIR)/../git_code/...)
# are skipped — gcc would accept them silently but they don't exist in the
# submodule.
#
# Chip-gated: KM0 uses inc_lp + FreeRTOS RTL8721D_LP; KM4 uses inc_hp +
# FreeRTOS RTL8721D_HP. The other ~30 paths are chip-agnostic.
#
# Members linked via this INTERFACE:
#   - amebad_fwlib_headers      (project-local 69 chip-register / HAL headers)
#   - inc_lp / inc_hp           (Kconfig autoconf + board opts; chip-selected)
#   - ~30 SDK subdirs           (FreeRTOS, swlib, app, common/api, wlan, lwip,
#                                mbedtls, ...)

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

set(_sdk_base "${CMAKE_SOURCE_DIR}/platform/ameba-rtos-d")
set(_sdk_soc  "${_sdk_base}/component/soc/realtek/amebad")
set(_os       "${_sdk_base}/component/os")
set(_common   "${_sdk_base}/component/common")
set(_proj     "${_sdk_base}/project/realtek_amebaD_va0_example")

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
    # OS abstraction (non-kernel): freertos_service.h, os_dep, ...
    "${_os}"
    "${_os}/freertos"
    "${_os}/os_dep/include"
    # SoC-level
    "${_sdk_soc}/cmsis"
    "${_sdk_soc}/app/monitor/include"
    "${_sdk_soc}/app/xmodem"
    "${_sdk_soc}/swlib/include"
    "${_sdk_soc}/swlib/string"
    "${_sdk_soc}/misc"
    # common APIs
    "${_common}/api"
    "${_common}/api/platform"
    "${_common}/api/wifi"
    "${_common}/api/network"
    "${_common}/api/network/include"
    "${_common}/api/at_cmd"
    "${_common}/api/wifi/rtw_wpa_supplicant/src"
    "${_common}/test"
    "${_common}/example"
    "${_common}/utilities"
    # WLAN driver headers
    "${_common}/drivers/wlan/realtek/include"
    "${_common}/drivers/wlan/realtek/src/hci"
    "${_common}/drivers/wlan/realtek/src/osdep"
    "${_common}/drivers/wlan/realtek/src/hal"
    "${_common}/drivers/wlan/realtek/src/hal/OUTSRC"
    "${_common}/drivers/wlan/realtek/src/hal/rtl8711b"
    "${_common}/drivers/wlan/realtek/wlan_ram_map/rom"
    # network / lwip 1.4.1
    "${_common}/network"
    "${_common}/network/lwip/lwip_v1.4.1/src/include"
    "${_common}/network/lwip/lwip_v1.4.1/src/include/lwip"
    "${_common}/network/lwip/lwip_v1.4.1/src/include/netif"
    "${_common}/network/lwip/lwip_v1.4.1/src/include/ipv4"
    "${_common}/network/lwip/lwip_v1.4.1/port/realtek"
    "${_common}/network/lwip/lwip_v1.4.1/port/realtek/freertos"
    # INIC
    "${_common}/drivers/inic/rtl8711b"
    # SSL — polarssl + mbedtls (rtl8721d_ota.h default-on HTTPS_OTA_UPDATE
    # pulls mbedtls/version.h into every TU that includes ameba_soc.h)
    "${_common}/network/ssl/polarssl-1.3.8/include"
    "${_common}/network/ssl/ssl_ram_map/rom"
    "${_common}/network/ssl/mbedtls-2.4.0/include"
    "${_common}/network/ssl/mbedtls_ram_map/rom"
    # mbed HAL
    "${_common}/mbed/targets/hal/rtl8721d"
)
