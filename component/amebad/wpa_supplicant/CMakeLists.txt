# amebad wpa_supplicant — Realtek's rtw_wpa_supplicant tree: the WiFi
# supplicant CONFIG / GLUE layer (source) + its header surface + the two prebuilt
# supplicant cores. The SDK ships the config/glue as source and stripped the
# EAP-peer and WPS cores to prebuilt blobs — lib_eap.a and lib_wps.a — all now
# consolidated into this one component (was: component/amebad/eap + the wps blob
# under component/amebad/wlan).
#
# Symbol shape (why two separate archives, not one): lib_wps.a is the base — WPS
# plus the supplicant util layer (os_*, wpabuf_*, eap_* helpers, WPS_realloc);
# lib_eap.a (EAP methods) needs ~21 of those from lib_wps.a, and lib_wps.a needs
# 3 eap_* back, so they're mutually referential and link inside the KM4 image2
# `--start-group`. Both archives also bake in eap_common.o (6 shared eap_*
# symbols), so they stay as TWO archives — merging into one .a would
# multiply-define those once both objects are pulled. Only amebad_wps is linked
# today (WPA2-PSK / WPS); amebad_eap (WPA-Enterprise) is defined but not linked.
#
# Contents (vendored verbatim, SDK layout preserved so cross-includes resolve):
#   wpa_supplicant/  wifi_eap_config.c / wifi_p2p_config.c / wifi_wps_config.c
#                    (+ wifi_wps_config.h) — the public WiFi config API glue
#                    (set EAP identity/password, start WPS, P2P config).
#   src/crypto/      tls_polarssl.c + tls_polarssl_mbedtls030201.c (TLS adapter,
#                    one chosen by config) + tls.h.
#   src/wps/         wps_defs.h.   src/utils/  common/includes/os/uuid/wpabuf/
#                    wpa_debug.h (+ rom/rom_wps_os.h).
#
# In the SDK these config .c are compiled by make/api/wifi INTO the wifi-api
# objects (not a standalone lib), so they are NOT built here — they belong to a
# future wifi-api component. This file only OWNS the tree and exposes the header
# surface (INTERFACE) that lib_eap.a / lib_wps.a consumers + those wrappers need.
#
# KM4-only (HP). Not referenced by the minimal driver-only image2 yet (no WiFi).
# Resync from component/common/api/wifi/rtw_wpa_supplicant when the
# platform/ameba-rtos-d pointer moves.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

# Header-only INTERFACE: the supplicant include surface. Consumers (the future
# wifi-api component, or anything linking lib_eap/lib_wps) pick it up by linking
# amebad_wpa_supplicant.
add_library(amebad_wpa_supplicant INTERFACE)
target_include_directories(amebad_wpa_supplicant SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/wpa_supplicant"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/crypto"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/wps"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/utils"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/utils/rom")

# --- prebuilt supplicant cores (KM4/HP blobs, no source) ---------------------
# GLOBAL so app_km4's link group (and future WiFi components) can link them.
# WPS core + supplicant util layer; carries the wlan dir on its INTERFACE
# includes (preserved from when it lived under component/amebad/wlan).
add_library(amebad_wps STATIC IMPORTED GLOBAL)
set_target_properties(amebad_wps PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/lib_wps.a"
    INTERFACE_INCLUDE_DIRECTORIES "${AT_ROOT}/component/amebad/wlan")
# EAP peer / 802.1X methods (PEAP/TLS/TTLS/FAST/MSCHAPv2 + des/md4/sha1/rc4/
# ms_funcs). Mutually referential with amebad_wps -> same image2 --start-group.
# Defined but not linked yet (WPA-Enterprise; gated by CONFIG_ENABLE_EAP).
add_library(amebad_eap STATIC IMPORTED GLOBAL)
set_target_properties(amebad_eap PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/lib_eap.a")
