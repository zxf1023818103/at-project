# fhost — the Bouffalo wifi6 (full-host) WiFi driver for BL616/BL616CL, replacing wl80211.
#
# Under sustained TCP/OTA load the old wl80211/macsw prebuilt stack TX-stalls and hard-asserts in
# hal_machw_abs_timer_handler (unfixable in source; see the project_bl616_aws_ota memory). fhost is
# Bouffalo's mainline wifi6 stack — a different TX architecture (fhost_tx_process) — and exposes the
# SAME wifi_mgmr_* API, so src/main.cpp's wifi_port_* HAL is largely unchanged.
#
# Integration mirrors the old wl80211 one: prebuilt .a's + a thin glue. The four archives are:
#   libfhost_bl616[cl]_default.a  — the fhost core (prebuilt by Bouffalo; MAC-HW + fhost tx/rx + wifi_mgmr)
#   libwpa_supplicant.a           — the wpa_supplicant port (SDK-built from wifi6/wpa_supplicant/)
#   libmacsw_os_adapter.a         — rtos_al + platform glue (SDK-built from wifi6/macsw_os_adapter/)
#   libfhost_config.a             — fhost_config_buffer (SDK-built from wifi6/fhost_config/)
# The lwip net adapter (wifi6_lwip_adapter) is intentionally NOT linked — the net_al bridge to
# FreeRTOS+TCP is our own (component/bouffalo_sdk/freertos_tcp_port/bouffalo_netif.c, rewritten).
#
# These .a's have circular inter-dependencies; they resolve in bouffalo_sdk's final --start-group.

if(NOT ("${BOUFFALO_CHIP}" STREQUAL "bl616" OR "${BOUFFALO_CHIP}" STREQUAL "bl616cl"))
    message(FATAL_ERROR "fhost: only bl616 / bl616cl supported")
endif()

set(_wifi6 "${AT_ROOT}/platform/bouffalo_sdk/components/wireless/wifi6")

set(_fhost_core "${CMAKE_CURRENT_SOURCE_DIR}/lib/libfhost_${BOUFFALO_CHIP}_default.a")
# The macsw MAC-SW core (mac_vif_*/hal_machw_*/interrupt0_handler/macsw_mac_time_init) that fhost sits
# on — from the SDK platform tree (shared with the old wl80211; fhost drives it via a different TX
# process). Referenced in place (not copied); it is the CONFIG_MACSW_SELECT=default variant.
set(_macsw "${_wifi6}/../macsw/lib")
set(_fhost_libs
    "${_fhost_core}"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/libwpa_supplicant.a"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/libmacsw_os_adapter.a"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/libfhost_config.a"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/libmacsw_plat.a"   # wifi_task_create / wifi_syslog / macsw_platform_*
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/libutils.a"        # async_event_init/loop/register/post
    "${_macsw}/libmacsw_${BOUFFALO_CHIP}_default.a"
    "${_macsw}/libmacsw_config_${BOUFFALO_CHIP}_default.a")
foreach(_lib IN LISTS _fhost_libs)
    if(NOT EXISTS "${_lib}")
        message(FATAL_ERROR "fhost: missing prebuilt ${_lib}")
    endif()
endforeach()

# Prebuilt-.a integration (same as the old wl80211): the macsw platform shim (libmacsw_plat.a:
# wifi_task_create / wifi_syslog / macsw_platform_*) and the async_event bus (libutils.a) are the
# SDK-built .a's — compiling their sources here would drag the whole SDK phy/macsw include chain.
add_library(fhost INTERFACE)
target_include_directories(fhost INTERFACE
    "${_wifi6}/fhost/include"               # fhost_api.h / net_al.h / wifi_mgmr.h / wifi_mgmr_ext.h / fhost.h
    "${_wifi6}/macsw_os_adapter/include"    # rtos_al.h / platform_al.h
    "${AT_ROOT}/platform/bouffalo_sdk/components/wireless/macsw/inc"    # mac_types.h (pulled by wifi_mgmr_ext.h)
    "${AT_ROOT}/platform/bouffalo_sdk/components/utils/async_event")    # async_event.h (EV_WIFI event bus)
target_link_libraries(fhost INTERFACE ${_fhost_libs})
