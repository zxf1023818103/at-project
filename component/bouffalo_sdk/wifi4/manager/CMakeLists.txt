# wifi4_manager — Bouffalo bl60x_wifi_driver: the host-side Wi-Fi driver
# that talks to the prebuilt libfirmware.a (UMAC firmware) over IPC.
# Extracted from bouffalo_sdk/components/wireless/wifi4/manager/.
#
# Includes layout: bl60x_wifi_driver/include/ is the PUBLIC surface
# (wifi_mgmr_ext.h, wifi_mgmr_cli.h, wifi_pkt_hooks.h, lwiphooks.h).
# Everything else in bl60x_wifi_driver/ is PRIVATE — internal driver
# headers like bl_main.h, ipc_host.h ride along with the .c files.
#
# CFG_TXDESC=2 is the only manager-private compile def (the rest of the
# CFG_*/RW_* wall comes from wifi4_firmware INTERFACE propagation).
#
# Optional pkt-hook knobs:
#   CONFIG_WIFI_PKT_INPUT_HOOK     PKT_INPUT_HOOK
#   CONFIG_WIFI_PKT_OUTPUT_HOOK    PKT_OUTPUT_HOOK
#   CONFIG_NETBUS_WIFI_ENABLE      CFG_NETBUS_WIFI_ENABLE
#   CONFIG_BL_IOT_FW_STATISTIC     CFG_BL_STATISTIC

if(NOT "${BOUFFALO_CHIP}" STREQUAL "bl602")
    message(FATAL_ERROR "wifi4_manager: only BL602 is supported (BOUFFALO_CHIP=${BOUFFALO_CHIP})")
endif()

add_library(wifi4_manager STATIC
    bl60x_wifi_driver/ipc_host.c
    bl60x_wifi_driver/bl_cmds.c
    bl60x_wifi_driver/bl_irqs.c
    bl60x_wifi_driver/bl_main.c
    bl60x_wifi_driver/bl_mod_params.c
    bl60x_wifi_driver/bl_msg_rx.c
    bl60x_wifi_driver/bl_msg_tx.c
    bl60x_wifi_driver/bl_platform.c
    bl60x_wifi_driver/bl_rx.c
    bl60x_wifi_driver/bl_tx.c
    bl60x_wifi_driver/bl_utils.c
    bl60x_wifi_driver/stateMachine.c
    bl60x_wifi_driver/wifi.c
    bl60x_wifi_driver/wifi_mgmr.c
    bl60x_wifi_driver/wifi_mgmr_api.c
    bl60x_wifi_driver/wifi_mgmr_ext.c
    bl60x_wifi_driver/wifi_mgmr_profile.c
    bl60x_wifi_driver/wifi_netif.c
    bl60x_wifi_driver/wifi_mgmr_event.c
    bl60x_wifi_driver/wifi_pkt_hooks.c
)

target_include_directories(wifi4_manager
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/bl60x_wifi_driver/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/bl60x_wifi_driver"
)

target_compile_definitions(wifi4_manager
    PUBLIC
        BL_CHIP_NAME="bl602"
    PRIVATE
        CFG_TXDESC=2
)

if(CONFIG_BL_IOT_FW_STATISTIC EQUAL 1)
    target_compile_definitions(wifi4_manager PRIVATE CFG_BL_STATISTIC)
endif()

if(CONFIG_NETBUS_WIFI_ENABLE EQUAL 1)
    target_compile_definitions(wifi4_manager PRIVATE CFG_NETBUS_WIFI_ENABLE)
endif()

if(CONFIG_WIFI_PKT_INPUT_HOOK EQUAL 1)
    target_compile_definitions(wifi4_manager PRIVATE PKT_INPUT_HOOK)
endif()

if(CONFIG_WIFI_PKT_OUTPUT_HOOK EQUAL 1)
    target_compile_definitions(wifi4_manager PRIVATE PKT_OUTPUT_HOOK)
endif()

# Wi-Fi driver pulls heavily from the firmware archive's symbol set, plus
# the CFG_*/RW_* defines and headers it carries — link transitively.
# Lhal (bflb_irq.h), bflb_log (formatted printk), FreeRTOS (queues / tasks)
# are required at compile time. lwIP (netif/etharp.h, lwip/pbuf.h …) is
# also needed but isn't linked here — matching the pattern used by every
# other extracted Bouffalo component, lwIP gets onto the include / link
# line via the consumer ELF, not via a cross-tree dependency edge.
target_link_libraries(wifi4_manager
    PUBLIC
        wifi4_firmware
        bflb_lhal
        bflb_log
        freertos_kernel
    PRIVATE
        wifi4_internal_headers
)
