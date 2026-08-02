# fhost — the Bouffalo wifi6 (full-host) WiFi driver for BL616/BL616CL/BL618DG, replacing wl80211.
#
# Under sustained TCP/OTA load the old wl80211/macsw prebuilt stack TX-stalls and hard-asserts in
# hal_machw_abs_timer_handler (unfixable in source; see the project_bl616_aws_ota memory). fhost is
# Bouffalo's mainline wifi6 stack — a different TX architecture (fhost_tx_process) — and exposes the
# SAME wifi_mgmr_* API, so src/main.cpp's wifi_port_* HAL is largely unchanged.
#
# Only the CLOSED-SOURCE cores stay prebuilt:
#   libfhost_bl616[cl]_default.a       — the fhost core (MAC-HW + fhost tx/rx + wifi_mgmr; no source)
#   libmacsw[_config]_<chip>_default.a — the macsw MAC-SW core (no source)
# Everything the SDK builds from source is built from source HERE too (wpa_supplicant,
# macsw_os_adapter, fhost_config, macsw_plat, async_event) — per-chip correctness (bl616cl has no
# P/packed-SIMD extension, so bl616-built objects fault on it) then comes from the toolchain file
# instead of vendoring per-chip archives.
# The lwip net adapter (wifi6_lwip_adapter) is intentionally NOT linked — the net_al bridge to
# FreeRTOS+TCP is our own (component/bouffalo_sdk/freertos_tcp_port/bouffalo_netif.c, rewritten).
#
# These .a's have circular inter-dependencies; they resolve in bouffalo_sdk's final --start-group.

if(NOT ("${BOUFFALO_CHIP}" STREQUAL "bl616" OR "${BOUFFALO_CHIP}" STREQUAL "bl616cl"
        OR "${BOUFFALO_CHIP}" STREQUAL "bl618dg"))
    message(FATAL_ERROR "fhost: only bl616 / bl616cl / bl618dg supported")
endif()

set(_wifi6 "${AT_ROOT}/platform/bouffalo_sdk/components/wireless/wifi6")

set(_fhost_core "${CMAKE_CURRENT_SOURCE_DIR}/lib/libfhost_${BOUFFALO_CHIP}_default.a")
# The macsw MAC-SW core (mac_vif_*/hal_machw_*/interrupt0_handler/macsw_mac_time_init) that fhost sits
# on — from the SDK platform tree (shared with the old wl80211; fhost drives it via a different TX
# process). Referenced in place (not copied); it is the CONFIG_MACSW_SELECT=default variant.
set(_macsw "${_wifi6}/../macsw/lib")
set(_fhost_libs
    "${_fhost_core}"
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
if(CONFIG_LPAPP)
    # LP_FW=ON: unlock the LP_APP-gated body of platform_hook_beacon (macsw's per-beacon weak hook,
    # macsw_hook_beacon -> platform_hook_beacon). It feeds each received beacon's TSF/RTC timestamp
    # pair to lpfw_recal_rc32k (RC32K fine-trim) + lpfw_calculate_beacon_delay, which are the ONLY
    # setters of the 32k_trim_ready / bcn_delay_ready flags that tickless.c's
    # check_system_ready_for_LowPower gates every PDS15 entry on. Without LP_APP the hook compiles
    # to a bare `ret` (the vendored prebuilt libmacsw_plat.a had it empty too) and tickless never
    # sleeps while associated (millions of 32k-abort attempts, `tickless stats`). The SDK's own lp
    # builds set LP_APP globally (power_mgmt CMakeLists under CONFIG_TICKLESS_ACCURATE); we scope it
    # to the one TU with LP_APP-gated code. Also switches the hal_macsw_lp_* stubs to their real
    # gp_lp_env-backed bodies — safe: main.cpp runs bl_lp_init() before wifi_task_create().
    target_compile_definitions(macsw_plat PRIVATE LP_APP)
endif()
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

# ---- SDK-source companions (were vendored prebuilts; converted to source builds) ----------------
# All three mirror the SDK recipes (wifi6/<name>/CMakeLists.txt) verbatim: same source lists, same
# compile definitions, same include surface. Building from source (a) removes the per-chip vendored
# archives (the bl616 set contained P-extension code that faults on bl616cl), (b) makes proj.conf's
# CONFIG_MAC_TXQ/RXQ_DEPTH actually reach fhost_config_buffer.c, and (c) compiles wpa_supplicant
# against the project's real mbedTLS config instead of whatever the vendor build assumed.

# fhost_config — fhost_config_buffer.c materializes the TX/RX queue + buffer sizing from the
# CONFIG_* macros (bflb_autoconf), which is exactly why it must be built per-project.
add_library(fhost_config STATIC "${_wifi6}/fhost_config/fhost_config_buffer.c")
target_include_directories(fhost_config PRIVATE
    "${_wifi6}/fhost/include"
    "${_wifi6}/macsw_os_adapter/include"
    "${AT_ROOT}/platform/bouffalo_sdk/components/wireless/macsw/inc")
target_link_libraries(fhost_config PRIVATE
    bflb_autoconf                 # forced -include autoconf.h (CONFIG_MAC_TXQ_DEPTH / VIF/STA_MAX ...)
    "${BOUFFALO_CHIP}_phyrf"      # phy.h (pulled by macsw.h)
    freertos_config)

# macsw_os_adapter — rtos_al.c (FreeRTOS rtos_al_* glue; also carries the vendor's weak
# vApplicationGetIdleTaskMemory and a strong vApplicationStackOverflowHook, which
# freertos-kernel_config/freertos_hooks.c yields to via weak) + platform_bouffalo_sdk.c.
# platform_iotsdk.c is intentionally NOT built, same as the SDK recipe.
add_library(macsw_os_adapter STATIC
    "${_wifi6}/macsw_os_adapter/src/rtos_al.c"
    "${_wifi6}/macsw_os_adapter/src/platform_bouffalo_sdk.c")
target_compile_definitions(macsw_os_adapter PRIVATE BOUFFALO_SDK)
target_include_directories(macsw_os_adapter PRIVATE
    "${_wifi6}/macsw_os_adapter/include"
    "${_wifi6}/fhost/include"
    "${AT_ROOT}/platform/bouffalo_sdk/components/wireless/macsw/inc")
target_include_directories(macsw_os_adapter PRIVATE
    "${AT_ROOT}/platform/bouffalo_sdk/components/utils/async_event")  # async_event.h (platform glue)
target_link_libraries(macsw_os_adapter PRIVATE
    bflb_autoconf
    "${BOUFFALO_CHIP}_std"
    "${BOUFFALO_CHIP}_phyrf"      # phy.h (pulled by macsw.h)
    freertos_config)

# wpa_supplicant — the SDK's macsw port of upstream wpa_supplicant. Source list mirrors the SDK
# recipe with CONFIG_WFA off (wnm_sta/rrm/mbo/mbo_ap/wnm_ap excluded, like the shipped builds).
set(_wpa "${_wifi6}/wpa_supplicant")
add_library(wpa_supplicant STATIC
    "${_wpa}/wpa_supplicant/wpa_supplicant.c"
    "${_wpa}/wpa_supplicant/wps_supplicant.c"
    "${_wpa}/wpa_supplicant/events.c"
    "${_wpa}/wpa_supplicant/scan.c"
    "${_wpa}/wpa_supplicant/notify.c"
    "${_wpa}/wpa_supplicant/bss.c"
    "${_wpa}/wpa_supplicant/wmm_ac.c"
    "${_wpa}/wpa_supplicant/wpas_glue.c"
    "${_wpa}/wpa_supplicant/eap_register.c"
    "${_wpa}/wpa_supplicant/config.c"
    "${_wpa}/wpa_supplicant/config_none.c"
    "${_wpa}/wpa_supplicant/ctrl_iface.c"
    "${_wpa}/wpa_supplicant/ctrl_iface_macsw.c"
    "${_wpa}/wpa_supplicant/main_macsw.c"
    "${_wpa}/wpa_supplicant/op_classes.c"
    "${_wpa}/wpa_supplicant/bssid_ignore.c"
    "${_wpa}/wpa_supplicant/robust_av.c"
    "${_wpa}/wpa_supplicant/sme.c"
    "${_wpa}/wpa_supplicant/ap.c"
    "${_wpa}/src/utils/eloop_rtos.c"
    "${_wpa}/src/utils/common.c"
    "${_wpa}/src/utils/wpabuf.c"
    "${_wpa}/src/utils/os_macsw.c"
    "${_wpa}/src/utils/wpa_debug.c"
    "${_wpa}/src/utils/bitfield.c"
    "${_wpa}/src/utils/ip_addr.c"
    "${_wpa}/src/utils/crc32.c"
    "${_wpa}/src/utils/uuid.c"
    "${_wpa}/src/utils/base64.c"
    "${_wpa}/src/wpa_crypto/rc4.c"
    "${_wpa}/src/wpa_crypto/sha1-prf.c"
    "${_wpa}/src/wpa_crypto/sha1-pbkdf2.c"
    "${_wpa}/src/wpa_crypto/sha256-prf.c"
    "${_wpa}/src/wpa_crypto/sha256-kdf.c"
    "${_wpa}/src/wpa_crypto/aes-wrap.c"
    "${_wpa}/src/wpa_crypto/aes-unwrap.c"
    "${_wpa}/src/wpa_crypto/aes-omac1.c"
    "${_wpa}/src/wpa_crypto/random.c"
    "${_wpa}/src/wpa_crypto/tls_none.c"
    "${_wpa}/src/wpa_crypto/dh_group5.c"
    "${_wpa}/src/wpa_crypto/crypto_internal-modexp.c"
    "${_wpa}/src/wpa_crypto/crypto_mbedtls.c"
    "${_wpa}/src/wpa_crypto/crypto_mbedtls_misc.c"
    "${_wpa}/src/wpa_crypto/dh_groups.c"
    "${_wpa}/src/tls/bignum.c"
    "${_wpa}/src/common/sae.c"
    "${_wpa}/src/common/dragonfly.c"
    "${_wpa}/src/common/wpa_common.c"
    "${_wpa}/src/common/ieee802_11_common.c"
    "${_wpa}/src/common/hw_features_common.c"
    "${_wpa}/src/l2_packet/l2_packet_macsw.c"
    "${_wpa}/src/rsn_supp/wpa.c"
    "${_wpa}/src/rsn_supp/wpa_ie.c"
    "${_wpa}/src/rsn_supp/preauth.c"
    "${_wpa}/src/rsn_supp/pmksa_cache.c"
    "${_wpa}/src/eapol_supp/eapol_supp_sm.c"
    "${_wpa}/src/drivers/drivers.c"
    "${_wpa}/src/drivers/driver_common.c"
    "${_wpa}/src/drivers/driver_macsw.c"
    "${_wpa}/src/ap/hostapd.c"
    "${_wpa}/src/ap/wpa_auth_glue.c"
    "${_wpa}/src/ap/utils.c"
    "${_wpa}/src/ap/authsrv.c"
    "${_wpa}/src/ap/ap_config.c"
    "${_wpa}/src/ap/sta_info.c"
    "${_wpa}/src/ap/tkip_countermeasures.c"
    "${_wpa}/src/ap/ap_mlme.c"
    "${_wpa}/src/ap/ieee802_1x.c"
    "${_wpa}/src/ap/ieee802_11_auth.c"
    "${_wpa}/src/ap/ieee802_11_shared.c"
    "${_wpa}/src/ap/drv_callbacks.c"
    "${_wpa}/src/ap/ap_drv_ops.c"
    "${_wpa}/src/ap/beacon.c"
    "${_wpa}/src/ap/bss_load.c"
    "${_wpa}/src/ap/eap_user_db.c"
    "${_wpa}/src/ap/neighbor_db.c"
    "${_wpa}/src/ap/wmm.c"
    "${_wpa}/src/ap/ap_list.c"
    "${_wpa}/src/ap/ieee802_11.c"
    "${_wpa}/src/ap/hw_features.c"
    "${_wpa}/src/ap/dfs.c"
    "${_wpa}/src/ap/wpa_auth.c"
    "${_wpa}/src/ap/wpa_auth_ie.c"
    "${_wpa}/src/ap/pmksa_cache_auth.c"
    "${_wpa}/src/ap/ieee802_11_ht.c"
    "${_wpa}/src/ap/ieee802_11_vht.c"
    "${_wpa}/src/ap/ieee802_11_he.c"
    "${_wpa}/src/ap/wps_hostapd.c"
    "${_wpa}/src/ap/ctrl_iface_ap.c"
    "${_wpa}/src/eapol_auth/eapol_auth_sm.c"
    "${_wpa}/src/eap_server/eap_server.c"
    "${_wpa}/src/eap_server/eap_server_identity.c"
    "${_wpa}/src/eap_server/eap_server_methods.c"
    "${_wpa}/src/eap_common/eap_common.c"
    "${_wpa}/src/eap_common/eap_wsc_common.c"
    "${_wpa}/src/eap_peer/eap.c"
    "${_wpa}/src/eap_peer/eap_wsc.c"
    "${_wpa}/src/eap_peer/eap_methods.c"
    "${_wpa}/src/wps/wps.c"
    "${_wpa}/src/wps/wps_attr_build.c"
    "${_wpa}/src/wps/wps_dev_attr.c"
    "${_wpa}/src/wps/wps_attr_parse.c"
    "${_wpa}/src/wps/wps_registrar.c"
    "${_wpa}/src/wps/wps_common.c"
    "${_wpa}/src/wps/wps_attr_process.c"
    "${_wpa}/src/wps/wps_enrollee.c")
target_compile_definitions(wpa_supplicant PRIVATE
    CONFIG_MACSW_LWIP
    CONFIG_USE_MBEDTLS
    CONFIG_NO_STDOUT_DEBUG
    CONFIG_LWIP)   # include-path selection only (includes.h/build_config.h): picks <sys/socket.h>,
                   # which our syscall_config BSD-compat provides over FreeRTOS+TCP (no lwip here)
# SDK recipe: the "small stack" variant is only safe when EVERY mbedTLS primitive is HW-offloaded.
if(NOT (CONFIG_MBEDTLS_AES_USE_HW AND CONFIG_MBEDTLS_BIGNUM_USE_HW AND CONFIG_MBEDTLS_ECC_USE_HW
        AND CONFIG_MBEDTLS_SHA1_USE_HW AND CONFIG_MBEDTLS_SHA256_USE_HW AND CONFIG_MBEDTLS_SHA512_USE_HW))
    target_compile_definitions(wpa_supplicant PRIVATE WPA_SUPPLICANT_USE_NORMAL_STACK)
endif()
target_include_directories(wpa_supplicant PRIVATE
    "${_wpa}/wpa_supplicant"
    "${_wpa}/src"
    "${_wpa}/src/utils"
    "${_wpa}/src/wpa_crypto"
    "${_wpa}/src/l2_packet"
    "${_wpa}/src/rsn_supp"
    "${_wpa}/src/drivers"
    "${_wpa}/src/eap_peer"
    "${_wpa}/src/wps"
    "${_wpa}/src/eapol_supp"
    "${_wifi6}/fhost/include"
    "${_wifi6}/macsw_os_adapter/include"
    "${AT_ROOT}/platform/bouffalo_sdk/components/wireless/macsw/inc")
# Third-party code under our stricter warning set; the SDK also suppresses per-warning.
# -Os: the SDK builds wpa at -Os; at the project's -O3 the image grows ~60 KB past the FW
# partition. Management-plane code — size over speed.
# -w silences warnings, but GCC 14 (the Zephyr SDK toolchain bl618dg must use)
# PROMOTES several long-standing C warnings to hard errors — implicit function
# declarations, int<->pointer conversions, incompatible pointer types. Upstream
# wpa_supplicant trips all three, and it is vendored source we do not patch, so
# demote them back to warnings. Harmless on the GCC 10 toolchain (the flags are
# accepted and those diagnostics are warnings there anyway).
target_compile_options(wpa_supplicant PRIVATE -w -Os
    -Wno-error=implicit-function-declaration
    -Wno-error=int-conversion
    -Wno-error=incompatible-pointer-types)
target_link_libraries(wpa_supplicant PRIVATE
    bflb_autoconf                 # forced -include autoconf.h (BL616[CL] / CONFIG_*)
    "${BOUFFALO_CHIP}_phyrf"      # phy.h (pulled by macsw.h)
    mbedtls_cfg                   # the project mbedTLS config (MBEDTLS_USER_CONFIG_FILE + ALT sel)
    mbedcrypto                    # upstream mbedTLS include/ (crypto_mbedtls.c)
    syscall_config                # sys/socket.h BSD compat (CONFIG_LWIP include path)
    freertos_config)

# The async_event bus (async_event_init/loop/register/post — fhost's EV_WIFI event dispatch) is
# compiled FROM SOURCE (was the prebuilt libutils.a, of which the link only ever pulled these two
# objects). async_event.c is libc-only; rtos_al.c is the vTaskEnterCritical/vTaskExitCritical lock.
add_library(async_event STATIC
    "${AT_ROOT}/platform/bouffalo_sdk/components/utils/async_event/async_event.c"
    "${AT_ROOT}/platform/bouffalo_sdk/components/utils/async_event/rtos_al.c")
target_include_directories(async_event PRIVATE
    "${AT_ROOT}/platform/bouffalo_sdk/components/utils/async_event")  # async_event.h
target_link_libraries(async_event PRIVATE freertos_config)            # FreeRTOS.h

# Prebuilt-.a integration (same as the old wl80211): the fhost core + wpa_supplicant + os-adapter +
# config stay SDK-built .a's — compiling their sources here would drag the whole SDK phy/macsw
# include chain. macsw_plat + async_event (above) are the ones compiled from source.
add_library(fhost INTERFACE)
target_include_directories(fhost INTERFACE
    "${_wifi6}/fhost/include"               # fhost_api.h / net_al.h / wifi_mgmr.h / wifi_mgmr_ext.h / fhost.h
    "${_wifi6}/macsw_os_adapter/include"    # rtos_al.h / platform_al.h
    "${AT_ROOT}/platform/bouffalo_sdk/components/wireless/macsw/inc"    # mac_types.h (pulled by wifi_mgmr_ext.h)
    "${AT_ROOT}/platform/bouffalo_sdk/components/utils/async_event")    # async_event.h (EV_WIFI event bus)
target_link_libraries(fhost INTERFACE ${_fhost_libs} macsw_plat async_event
    wpa_supplicant macsw_os_adapter fhost_config)

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
    if(AT_ENABLE_NIMBLE)
        # `tickless stats` — BLE sleep-arbitration counters (bflb_power_mgmt/tickless_ble.c,
        # only compiled on NIMBLE=ON builds, so the subcommand is gated the same way).
        target_compile_definitions(lp_cli PRIVATE AT_LP_BLE_STATS=1)
    endif()
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
