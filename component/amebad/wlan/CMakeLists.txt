# amebad wlan — Realtek host-side WiFi driver: the public API headers
# (autoconf.h + wifi_constants/structures, wlan_bssdef, rtw_debug, drv_conf,
# rom_aes, wifi_performance_monitor) and the prebuilt driver libs
# (lib_wlan*.a + lib_wps.a — KM4/HP binary blobs, no source).
#
# NOTE: this is the HOST DRIVER layer, distinct from the on-chip WiFi MAC
# *firmware* blob (component/amebad/app_km0/lib_wifi_fw.a, KM0). lib_wifi_fw.a's
# 515 symbols are firmware internals and have ~zero overlap with these headers;
# lib_wlan*.a/lib_wps.a are what actually implement the wifi_* API the headers
# declare. They are KM4-only (project_hp/asdk/lib/application).
#
# lib_wlan.a ships in FOUR mutually-exclusive build variants — same 156 object
# files, different CONFIG, link exactly ONE (the SDK Makefile swaps them by
# CONFIG_MP_INCLUDED / lite / lp):
#   (default)  lib_wlan.a      standard
#   lp         lib_wlan_lp.a   low-power (RF close/open, low-power connect timeouts)
#   lt         lib_wlan_lt.a   lite / long-period tickless + wowlan keepalive
#   mp         lib_wlan_mp.a   mass-production RF test (Hal_Set*Tx, mp_start_test, read_rfreg)
# Pick one with -DAMEBAD_WLAN_VARIANT=<lp|lt|mp> (empty = standard).
#
# This file owns the prebuilt IMPORTED libs (KM4-gated). The headers are exposed
# broadly via amebad_sdk_includes (which points at this dir) because autoconf.h
# is the Realtek-quirk home of the global Kconfig autoconf — pulled by ~6 TUs on
# BOTH cores (sys_arch / main.h / lwip_netconf / usrcfg) even with WiFi off. The
# IMPORTED libs also carry this dir on their INTERFACE includes for a direct
# consumer.
#
# WIP: the minimal driver-only image2 does NOT link these yet (no WiFi stack).
# They come into the KM4 image2 link when the WiFi/lwip/ethernetif path is wired.
# Resync the .a from project_hp/asdk/lib/application when the
# platform/ameba-rtos-d submodule pointer moves.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

set(AMEBAD_WLAN_VARIANT "" CACHE STRING
    "AmebaD WiFi driver variant: empty=standard, lp=low-power, lt=lite, mp=RF-test")
if(AMEBAD_WLAN_VARIANT STREQUAL "")
    set(_wlan_lib "lib_wlan.a")
elseif(AMEBAD_WLAN_VARIANT MATCHES "^(lp|lt|mp)$")
    set(_wlan_lib "lib_wlan_${AMEBAD_WLAN_VARIANT}.a")
else()
    message(FATAL_ERROR
        "AMEBAD_WLAN_VARIANT='${AMEBAD_WLAN_VARIANT}' invalid (expected '', lp, lt, mp)")
endif()

# Header surface carried by the driver libs: the flat public API headers (this
# dir), the driver-internal osdep headers (osdep/: skbuff/wlan_intf/lwip_intf/
# wireless/bt_intf/wrapper.h), the ROM wlan-ram-map header (rom/), and the
# core/option headers vendored beside the glue sources (src/: rtw_opt_crypto_ssl.h).
set(_wlan_inc
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/osdep"
    "${CMAKE_CURRENT_SOURCE_DIR}/rom"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

# GLOBAL so the sibling app_km4 dir (and future WiFi components) can link them.
add_library(amebad_wlan STATIC IMPORTED GLOBAL)
set_target_properties(amebad_wlan PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/${_wlan_lib}"
    INTERFACE_INCLUDE_DIRECTORIES "${_wlan_inc}")

# amebad_wps (lib_wps.a — the WPS/supplicant core) moved to the wpa_supplicant
# component, consolidated with lib_eap.a + the supplicant source/headers.

# --- WiFi driver SOURCE glue (not in lib_wlan.a) -----------------------------
# The 7 driver source files the SDK keeps outside the prebuilt lib so they stay
# tunable: the lwip<->driver bridge (osdep/lwip_intf.c) and the core/option
# tables (skb pool sizing, RF params for RTL8721D, WLAN ram map, TX power by
# rate / power limit, crypto-ssl option). Compiled here into amebad_wlan_glue;
# joins the KM4 image2 link group alongside the prebuilt libs.
add_library(amebad_wlan_glue STATIC EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_SOURCE_DIR}/src/lwip_intf.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/rtw_opt_skbuf.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/rtw_opt_rf_para_rtl8721d.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/rtw_opt_wlan_ram_map.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/rtw_opt_power_by_rate.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/rtw_opt_power_limit.c"
    # rtw_opt_crypto_ssl.c — the WiFi driver's TLS/crypto option (WPA-Enterprise /
    # EAP-TLS connect path). Written against the SDK's mbedtls 2.4.0 (uses the now-
    # private mbedtls_ecp_point.X/Y/Z, removed in the shared mbedtls 3.6.4). Not on
    # the scan path, so it stays vendored-but-uncompiled here; wire it (with a 3.x
    # port) when WPA-Enterprise/connect lands. Its symbols are weak-stubbed if the
    # driver references them at link.
)
# GLOBAL visibility for app_km4's link group.
set_target_properties(amebad_wlan_glue PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_link_libraries(amebad_wlan_glue PUBLIC
    amebad_sdk_includes
    amebad_wlan
    amebad_osdep
    amebad_netif_shim        # vendored struct netif + eth_drv_sg (lwip removed)
    amebad_wifi_headers      # lwip_intf.c -> <wifi/wifi_util.h>
    FreeRTOS
)
target_compile_options(amebad_wlan_glue PRIVATE
    -fcommon   # legacy Realtek tentative-definition globals (see wifi/CMakeLists)
    -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
    -Wno-sign-compare -Wno-unused-but-set-variable)
