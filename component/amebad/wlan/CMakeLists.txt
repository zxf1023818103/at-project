# amebad wlan — Realtek host-side WiFi driver: the public API headers
# (autoconf.h + wifi_constants/structures, wlan_bssdef, rtw_debug, drv_conf,
# rom_aes, wifi_performance_monitor) and the prebuilt driver libs
# (lib_wlan*.a + lib_wps.a — KM4/HP binary blobs, no source).
#
# NOTE: this is the HOST DRIVER layer, distinct from the on-chip WiFi MAC
# *firmware* blob (component/amebad/prebuilt/lib_wifi_fw.a, KM0). lib_wifi_fw.a's
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

# GLOBAL so the sibling app_km4 dir (and future WiFi components) can link them.
add_library(amebad_wlan STATIC IMPORTED GLOBAL)
set_target_properties(amebad_wlan PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/${_wlan_lib}"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(amebad_wps STATIC IMPORTED GLOBAL)
set_target_properties(amebad_wps PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/lib_wps.a"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
