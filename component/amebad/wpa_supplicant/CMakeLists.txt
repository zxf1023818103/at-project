# amebad wpa_supplicant — Realtek's rtw_wpa_supplicant tree: the WiFi
# supplicant CONFIG / GLUE layer (source) plus its header surface. This is the
# part of the supplicant the SDK still ships as source; the EAP-peer and WPS
# cores were stripped to prebuilt blobs — lib_eap.a (component/amebad/eap) and
# lib_wps.a (component/amebad/wlan) — which this layer sits on top of.
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
