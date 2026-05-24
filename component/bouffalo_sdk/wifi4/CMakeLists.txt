# wifi4 — umbrella for the Bouffalo Wi-Fi 4 stack (BL602 only). Extracted
# from bouffalo_sdk/components/wireless/wifi4/.
#
# Six sublibs, all with their own CMakeLists:
#   firmware/        INTERFACE   prebuilt libfirmware.a + the CFG_*/RW_*/NX_*
#                                define wall everyone else compiles against
#   manager/         STATIC      bl60x_wifi_driver — host-side driver
#   wpa_supplicant/  STATIC      bl_supplicant — pruned wpa_supplicant fork
#   blcrypto_suite/  STATIC      HACC accelerator glue (AES/SHA/ECC)
#   bflb_adapter/    STATIC      platform / RTOS / CLI / iperf ALs
#   wifi_hosal/      STATIC      HOSAL shim + bl602 chip port + bflb_pm
#
# This file gives consumers a single target — `wifi4` — that links them
# all in dependency-order so any user of "the Wi-Fi 4 stack" only needs
# to link one thing.
#
# BUILD STATUS (2026-05-24): configure passes for the bouffalo_sdk
# umbrella with -DBOUFFALO_CHIP=bl602 -DCONFIG_RF=y -DCONFIG_WIFI4=y.
# Full compile is blocked on, in order:
#   1. lwip isn't wired into the chip-toolchain component/ tree yet.
#      component/lwip/CMakeLists.txt still uses SDK sdk_* macros, so the
#      `lwipcore` target doesn't exist. wifi_mgmr_ext.h (manager's PUBLIC
#      header) #include <lwip/netif.h>, so every consumer of wifi4 needs
#      lwIP on the include path.
#   2. Utility components not yet extracted from
#      platform/bouffalo_sdk/components/utils/{getopt,list,log,…}.
#      Manager / supplicant / adapter pull headers from utils_list,
#      utils_log, etc.
#   3. Pre-existing extraction gaps (bflb_power_mgmt → lwip/tcpip.h,
#      bflb_sys → bl602_lp.h) are unrelated to wifi4 but block any
#      transitive build that drags those components in.
#
# CLI bits were removed at user request — cli_al.c, wifi_mgmr_cli.c, and
# wifi_mgmr_cli.h are gone; wifi_mgmr_ext.c/h were patched to drop the
# two cli_* call sites / declarations.

if(NOT "${BOUFFALO_CHIP}" STREQUAL "bl602")
    message(FATAL_ERROR "wifi4: only BL602 is supported (BOUFFALO_CHIP=${BOUFFALO_CHIP})")
endif()

# wifi4_internal_headers — INTERFACE target that aggregates every sublib's
# public include surface. The upstream SDK relied on a global include
# space (sdk_add_include_directories adds to a project-wide set), so
# the sublibs freely #include each other's headers in every direction.
# A native-CMake port can't express that with target_link_libraries
# without inducing cycles. Solution: each sublib privately uses this
# INTERFACE for header access, and only adds real symbol-link
# relationships via target_link_libraries.
add_library(wifi4_internal_headers INTERFACE)
target_include_directories(wifi4_internal_headers INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/firmware/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/manager/bl60x_wifi_driver/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/wpa_supplicant/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/wpa_supplicant/include/bl_supplicant"
    "${CMAKE_CURRENT_SOURCE_DIR}/blcrypto_suite/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/bflb_adapter"
    "${CMAKE_CURRENT_SOURCE_DIR}/bflb_adapter/bflb_os_adapter"
    "${CMAKE_CURRENT_SOURCE_DIR}/bflb_adapter/bflb_os_adapter/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/bflb_adapter/bflb_os_adapter/include/bflb_os_adapter"
    "${CMAKE_CURRENT_SOURCE_DIR}/bflb_adapter/tlv"
    "${CMAKE_CURRENT_SOURCE_DIR}/bflb_adapter/blcrypto_sec_pka"
    "${CMAKE_CURRENT_SOURCE_DIR}/wifi_hosal/include"
)
# Pull in the small utility libs the wifi4 sources reach for via the
# SDK's old global include space (utils_list.h, utils_hex.h, utils_log.h,
# …). PUBLIC-link propagation gives every wifi4 sublib that PRIVATEly
# uses wifi4_internal_headers transparent access to these headers.
# lwip is here too — wifi_mgmr_ext.h and a half-dozen .c files #include
# <lwip/…> directly. Linking lwipcore brings its PUBLIC include surface
# (lwip/* + lwipopts.h via bflb_lwip_port) onto the line.
target_link_libraries(wifi4_internal_headers INTERFACE
    bflb_list
    bflb_math
    bflb_log
    bflb_ring_buffer
    lwipcore
    lwip_dhcpd
)
# wpa_supplicant's crypto layer #includes <mbedtls/aes.h> unconditionally
# (USE_MBEDTLS_CRYPTO is hardwired in supplicant_opt.h). The hw_acc
# target carries the public include path for component/mbedtls/include
# (and the matching MBEDTLS_*_ALT macros via bflb_mbedtls_config).
if(TARGET bflb_mbedtls_hw_acc)
    target_link_libraries(wifi4_internal_headers INTERFACE bflb_mbedtls_hw_acc)
endif()

add_subdirectory(firmware)
add_subdirectory(blcrypto_suite)
add_subdirectory(bflb_adapter)
add_subdirectory(wifi_hosal)
add_subdirectory(manager)
add_subdirectory(wpa_supplicant)

add_library(wifi4 INTERFACE)
target_link_libraries(wifi4 INTERFACE
    wifi4_manager
    wifi4_wpa_supplicant
    wifi4_blcrypto_suite
    wifi4_bflb_adapter
    wifi4_wifi_hosal
    wifi4_firmware
)
