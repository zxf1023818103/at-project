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
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/libutils.a"        # async_event_init/loop/register/post
    "${_macsw}/libmacsw_${BOUFFALO_CHIP}_default.a"
    "${_macsw}/libmacsw_config_${BOUFFALO_CHIP}_default.a")
foreach(_lib IN LISTS _fhost_libs)
    if(NOT EXISTS "${_lib}")
        message(FATAL_ERROR "fhost: missing prebuilt ${_lib}")
    endif()
endforeach()

# The macsw platform shim (macsw_plat.c: wifi_task_create / wifi_syslog / bl_tpc_* / hal_macsw_lp_* /
# macsw_platform_*) is compiled FROM SOURCE (was the prebuilt libmacsw_plat.a). It sits in the SDK's
# wireless tree and pulls the macsw + phy + lhal + freertos include chain, so it links the project's
# wrappers for those (bflb_autoconf for the forced -include autoconf.h with BL616/CONFIG_*, the chip
# std/phyrf, bflb_sys, freertos_config) and adds the macsw/os-adapter headers directly. The config
# macros it branches on (CFG_BL_WIFI_PS_ENABLE, MACSW_TWT) come from macsw_config.h/macsw.h, and
# CONFIG_MACSW_SELECT_INCLUDE picks the SAME 'default' variant the prebuilt fhost/macsw .a's used, so
# it stays ABI-matched with them.
add_library(macsw_plat STATIC "${AT_ROOT}/platform/bouffalo_sdk/components/wireless/macsw_plat.c")
target_compile_definitions(macsw_plat PRIVATE CONFIG_MACSW_SELECT_INCLUDE="macsw_default_config.h")
# Headers via the SDK/config libs. macsw_plat.c's calls (bflb_rtc_get_time, bl_lp_*, phy/wl_api,
# FreeRTOS) resolve at the final image link from the bouffalo_sdk umbrella, same as the prebuilt
# libmacsw_plat.a's unresolved refs did.
target_include_directories(macsw_plat PRIVATE
    "${AT_ROOT}/platform/bouffalo_sdk/components/wireless/macsw/inc"  # macsw.h/coexm.h/mac_types.h/macsw_plat.h/macsw_config.h
    "${_wifi6}/macsw_os_adapter/include")                            # rtos_al.h / platform_al.h
target_link_libraries(macsw_plat PRIVATE
    bflb_autoconf                 # forced -include autoconf.h (BL616 / CONFIG_* global config)
    "${BOUFFALO_CHIP}_std"        # bl616_hbn.h + (transitively) bflb_lhal -> bflb_rtc.h
    "${BOUFFALO_CHIP}_phyrf"      # wl_api.h / phy.h
    bflb_sys                      # bl_lp.h (+ bl616_lp.h)
    freertos_config)              # FreeRTOS.h / task.h

# Prebuilt-.a integration (same as the old wl80211): the fhost core + wpa_supplicant + os-adapter +
# config + async_event bus (libutils.a) stay SDK-built .a's — compiling their sources here would drag
# the whole SDK phy/macsw include chain. macsw_plat (above) is the one compiled from source.
add_library(fhost INTERFACE)
target_include_directories(fhost INTERFACE
    "${_wifi6}/fhost/include"               # fhost_api.h / net_al.h / wifi_mgmr.h / wifi_mgmr_ext.h / fhost.h
    "${_wifi6}/macsw_os_adapter/include"    # rtos_al.h / platform_al.h
    "${AT_ROOT}/platform/bouffalo_sdk/components/wireless/macsw/inc"    # mac_types.h (pulled by wifi_mgmr_ext.h)
    "${AT_ROOT}/platform/bouffalo_sdk/components/utils/async_event")    # async_event.h (EV_WIFI event bus)
target_link_libraries(fhost INTERFACE ${_fhost_libs} macsw_plat)

# LP_FW=ON (CONFIG_LPAPP): the vendored lp_fw shared-function library. bl_lp_fw_init() (bflb_sys, via
# shared_func_init) hands the lp_fw its beacon-delay / bcn-check helpers — lpfw_calculate_beacon_delay,
# lpfw_beacon_delay_sliding_win_get_average, etc. — which live only here (the default fhost/macsw
# archives are built without the lp path). Its own refs (bl_lp_* / iot2lp_para / CPU_*) resolve from
# bflb_sys + the chip std at the umbrella link.
if(CONFIG_LPAPP)
    set(_lpfw_priv "${AT_ROOT}/platform/bouffalo_sdk/components/wireless/lpfw_private/liblpfw_private.a")
    if(NOT EXISTS "${_lpfw_priv}")
        message(FATAL_ERROR "fhost: CONFIG_LPAPP set but missing ${_lpfw_priv}")
    endif()
    target_link_libraries(fhost INTERFACE "${_lpfw_priv}")

    # `lpsleep` console command (lp_cli.c): one on-demand PDS15 deep-sleep cycle. bl_lp_fw_init()
    # re-seeds iot2lp_para, then bl_lp_fw_enter() enters PDS; board_recovery() recovers on wake.
    add_library(lp_cli STATIC "${CMAKE_CURRENT_SOURCE_DIR}/lp_cli.c")
    target_link_libraries(lp_cli PRIVATE
        bflb_autoconf                     # -include autoconf.h (CFG_* / BL616)
        bflb_sys                          # bl_lp.h (bl_lp_fw_cfg_t / lpfw_cfg / bl_lp_fw_enter)
        "${BOUFFALO_CHIP}_std"            # bl616_pds/hbn/aon/pm.h + pm_pds_* / AON_* / HBN_* (bare PDS path)
        bflb_power_mgmt                   # tickless.h + tickless_enter/exit (`tickless` command)
        freertos_cli                      # FreeRTOS_CLI.h
        "${BOUFFALO_CHIP}_board_config")  # board.h (board_recovery)
    target_link_libraries(fhost INTERFACE lp_cli)

    # app_clock_init() — F32K/RC32K startup bring-up that clocks the HBN RTC so PDS15 self-wakes.
    # Compiled into bflb_power_mgmt under CONFIG_LPAPP; main.cpp runs it from a post-scheduler task.
    target_link_libraries(fhost INTERFACE bflb_power_mgmt)
endif()
