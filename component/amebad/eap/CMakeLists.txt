# amebad eap — Realtek EAP peer / 802.1X supplicant for WPA-Enterprise:
# prebuilt lib_eap.a (KM4/HP binary blob, no source). 21 objects =
# EAP methods (PEAP / TLS / TTLS / FAST / MSCHAPv2) + their crypto helpers
# (des / md4 / sha1 / rc4 / ms_funcs). Built in the SDK from
# component/common/api/wifi/rtw_wpa_supplicant/src/{eap_common,eap_peer,
# crypto,utils} + wpa_supplicant/eap_protocol_handler.c — a distinct
# `network/eap` module, separate from the core WiFi driver (component/amebad/wlan).
#
# Own component (not folded into wlan/) mirroring the SDK's network/eap grouping:
# EAP is an optional WPA-Enterprise feature gated by CONFIG_ENABLE_EAP, not part
# of the base driver. No headers of its own — the EAP config API (wifi_eap_config.h
# etc.) belongs to the wpa_supplicant / wifi-api surface, vendored when that lands.
#
# KM4-only (project_hp/asdk/lib/application). WIP: the minimal driver-only image2
# does NOT link this yet (no WiFi/supplicant stack). It joins the KM4 image2 link
# group when WPA-Enterprise is wired. Resync the .a from
# project_hp/asdk/lib/application when the platform/ameba-rtos-d pointer moves.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

# GLOBAL so the sibling app_km4 dir (and future WiFi/supplicant components) can link it.
add_library(amebad_eap STATIC IMPORTED GLOBAL)
set_target_properties(amebad_eap PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/lib_eap.a")
