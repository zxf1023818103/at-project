# wl80211 — Bouffalo's 802.11 driver + wifi_mgmr API surface for
# BL616 / BL616CL (and BL618DG, which we don't ship). Extracted from
# bouffalo_sdk/components/wireless/wl80211/.
#
# Layout:
#   wifi_mgmr.c / wifi_mgmr_cli.c / country.c / wl80211_platform.c /
#   lwip.c / supplicant.c  -- the C wrapper layer (CLI + mgmr API +
#   lwIP integration + supplicant glue + country/regdomain tables)
#   lib/libwl80211_${BOUFFALO_CHIP}.a   -- prebuilt driver core
#   include/   -- public headers (wifi_mgmr.h / wifi_mgmr_ext.h /
#                 wl80211.h / supplicant.h / ieee802_11_defs.h / timeout.h)
#   macsw/wl80211_mac.h -- private MAC header
#
# Skipped from upstream:
#   net80211/   FreeBSD net80211 source port that's actively in-progress in
#               upstream (per wl80211/CLAUDE.md: compiles but not functional).
#               We use the prebuilt .a instead.
#   nuttx.c / rtos_al_nuttx.c   NuttX adapters — we use FreeRTOS.
#
# Consumers: src/main.cpp + the 4 wifi_mgmr_sta_ps_* APIs that
# pm_manager.c wants (gated behind CONFIG_PM_MANAGER in bflb_power_mgmt).

if(NOT ("${BOUFFALO_CHIP}" STREQUAL "bl616" OR
        "${BOUFFALO_CHIP}" STREQUAL "bl616cl"))
    message(FATAL_ERROR
        "wl80211: only bl616 / bl616cl prebuilt .a shipped in this fork.")
endif()

set(_wl80211_lib "${CMAKE_CURRENT_SOURCE_DIR}/lib/libwl80211_${BOUFFALO_CHIP}.a")
if(NOT EXISTS "${_wl80211_lib}")
    message(FATAL_ERROR "wl80211: missing prebuilt ${_wl80211_lib}")
endif()

# lwip.c (the lwIP netif glue: wl80211_tcpip_input RX up-call + wl80211_output TX +
# wl80211_lwip_init) is dropped — the network stack is switching to FreeRTOS-Plus-TCP.
# A new freertos_tcp_port will re-supply wl80211_tcpip_input / wl80211_lwip_init (the
# symbols the prebuilt libwl80211 calls) on top of a FreeRTOS-Plus-TCP NetworkInterface.
add_library(wl80211 STATIC
    wifi_mgmr.c
    wifi_mgmr_cli.c
    country.c
    wl80211_platform.c
    supplicant.c
    # weak no-op stubs for the lwIP-era _wifi_mgmr_sta_link_up/down + _wifi_mgmr_ap_*_dhcpd hooks that
    # lived in the dropped lwip.c (FreeRTOS-Plus-TCP netif owns link/DHCP now); overridable.
    wl80211_link_stubs.c
)

target_include_directories(wl80211 PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_include_directories(wl80211 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/macsw"
)

# Prebuilt .a propagated PUBLIC so the final ELF link picks it up
# transitively (the symbols it provides are referenced both from our
# wrapper sources here and from app code via the include/ headers).
target_link_libraries(wl80211 PUBLIC
    "${_wl80211_lib}"
    bflb_lhal
    ${BOUFFALO_CHIP}_std
    freertos_port
    # wl80211.h pulls "queue/queue.h" + "tree/tree.h" (header-only utils)
    bflb_queue
    bflb_tree
    # wifi_mgmr.c / wifi_mgmr_cli.c #include "macsw.h"
    macsw
    # wl80211_platform.c #include "mm.h"
    tlsf_config
    # (bflb_async_event deleted: wifi_mgmr.c / wl80211_platform.c dispatch WiFi events directly now)
    # wifi_mgmr_cli.c #include "utils_getopt.h"
    bflb_getopt
    # supplicant.c #include "bl_wpa.h"
    bl_wpa_supplicant
    # wifi_mgmr.c / supplicant.c #include "log.h"
    rxi_log
)

# Upstream wraps every wl80211 source in -Werror -Wno-unused-function
# -Wno-unused-variable -Wstrict-prototypes. The wrapper .c has hostap-
# port-style code that triggers a chunk of warnings (unused statics in
# country tables, prototypes that don't follow strict-prototypes).
# Match upstream so we don't turn benign noise into fatal errors.
target_compile_options(wl80211 PRIVATE
    -Wno-unused-function
    -Wno-unused-variable
)

unset(_wl80211_lib)
