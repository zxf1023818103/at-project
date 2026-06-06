# amebad_hci — Realtek AmebaD (RTL8721D) BLE controller bring-up + HCI transport +
# UART HCI bridge. The Realtek upper stack (btgap.a) is NOT used: this is just the
# controller bring-up (firmware-patch download + HCI reset) and a raw HCI pipe,
# intended to sit under a replacement host stack (e.g. NimBLE).
#
# Layout: flat (house style) — HCI transport + osif(FreeRTOS) + UART bridge + board
# glue + the controller firmware blobs + the referenced headers, all vendored
# straight into this folder from
# platform/ameba-rtos-d/component/common/bluetooth/realtek/sdk/ (no basename
# collisions, no subpath #includes, so one flat include dir resolves everything).
# Vendored files were de-"bt"-named to hci_* to match the component; the one
# collision (the SDK's bt_board.h vs our hci_board.h) became hci_board_def.h.
#
# How bring-up works without btgap.a: bte_init() lived in btgap.a and merely called
# hci_tp_open() (our code) + started the host. hci_nostack.c calls hci_tp_open()
# directly and re-provides the 4 utility symbols btgap.a used to define
# (os_delay / os_mem_free / os_mem_zalloc_intern / trace_log_buffer). In-tree glue:
# hci_dtm.c (bring-up + polling HCI bridge), hci_nostack.c, hci_fw_stub.c,
# hci_compat.c, hci_syscalls.c.
#
# KM4-only (the HCI transport + the LOGUART the bridge hijacks are KM4's at
# runtime). EXCLUDE_FROM_ALL until app_km4 links it.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

# ---- Header surface (one flat include dir — this folder) ----
add_library(amebad_hci_headers INTERFACE)
target_include_directories(amebad_hci_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}")

# ---- Controller firmware blob: normal (BLE DTM) vs mp (RF mfg test) ----
# OFF (default) downloads hci_normal_patch.c (standard BLE DTM); ON downloads
# hci_mp_patch.c and defines CONFIG_MP_INCLUDED so hci_find_patch_address() selects
# the MP patch and hci_dtm.c logs accordingly.
#   cmake build/km4 -DAMEBAD_ENABLE_HCI=ON -DAMEBAD_HCI_FW_MP=ON
#
# NB: the blob .c itself is NOT compiled into this archive — it is added to the
# app_km4 EXECUTABLE (see app_km4/CMakeLists.txt) so its STRONG rtlbt_(mp_)fw / _len
# always link and override the weak {0} stubs in hci_fw_stub.c. A strong blob inside
# this archive would not get pulled (the weak stubs already satisfy the references),
# leaving the controller with an empty patch.
option(AMEBAD_HCI_FW_MP "Use the MP/RF-test BT controller firmware instead of normal BLE DTM" OFF)
if(AMEBAD_HCI_FW_MP)
    set(_fw_def CONFIG_MP_INCLUDED=1)
else()
    set(_fw_def)
endif()

# ---- Bare-ROM probe: bring up the controller WITHOUT downloading the patch ----
# Drops the baudrate + download-patch table steps and the patch-parse in the rom
# check; the controller stays at its 115200 boot baud. Used to probe what the raw
# silicon ROM exposes.
#   cmake build/km4 -DAMEBAD_ENABLE_HCI=ON -DAMEBAD_HCI_SKIP_PATCH=ON
option(AMEBAD_HCI_SKIP_PATCH "Bring up the BT controller WITHOUT the firmware patch (bare ROM probe)" OFF)
if(AMEBAD_HCI_SKIP_PATCH)
    set(_skip_patch_def AMEBAD_HCI_SKIP_PATCH=1)
else()
    set(_skip_patch_def)
endif()

# ---- HCI transport + osif + bridge + board + bring-up glue (no btgap.a) ----
add_library(amebad_hci STATIC EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_SOURCE_DIR}/osif_freertos.c"   # the 49 osif_*
    "${CMAKE_CURRENT_SOURCE_DIR}/hci_adapter.c"     # hci_tp_open/close/send/recv (hci_tp_open = full bring-up)
    "${CMAKE_CURRENT_SOURCE_DIR}/hci_process.c"     # HCI_PROCESS_TABLE (patch dl + reset)
    "${CMAKE_CURRENT_SOURCE_DIR}/trace_task.c"      # bt_trace_init / trace_print
    "${CMAKE_CURRENT_SOURCE_DIR}/cycle_queue.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/hci_uart_bridge.c" # provides bt_uart_tx (referenced by hci_uart.c)
    "${CMAKE_CURRENT_SOURCE_DIR}/hci_board.c"       # bt_reset/bt_power_on, hci_board_init, patch select
    "${CMAKE_CURRENT_SOURCE_DIR}/hci_uart.c"        # UART1 HCI driver, set_hci_uart_out
    "${CMAKE_CURRENT_SOURCE_DIR}/platform_utils.c"  # platform_random
    "${CMAKE_CURRENT_SOURCE_DIR}/trace_uart.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/hci_fwconfig.c"    # rtlbt_config + rltk_bt_get_normal/mp_patch
    "${CMAKE_CURRENT_SOURCE_DIR}/hci_nostack.c"     # hci_tp_open bring-up + the 4 util syms btgap.a used to define
    # NB: the firmware blob (hci_normal_patch.c / hci_mp_patch.c) is in the app_km4
    # EXECUTABLE, not here — see the comment on AMEBAD_HCI_FW_MP above.
    "${CMAKE_CURRENT_SOURCE_DIR}/hci_fw_stub.c"     # weak rtlbt_(mp_)fw[/_len] (replaces rtl8721d_bt.c -> no drv_types.h)
    "${CMAKE_CURRENT_SOURCE_DIR}/hci_compat.c"      # UART_RxMonitorSatusGet + printf->ROM (no newlib stdio)
    "${CMAKE_CURRENT_SOURCE_DIR}/hci_dtm.c"         # in-tree bring-up + polling HCI bridge
    # NB: hci_syscalls.c (newlib _write/_sbrk/... stubs) is added to the app_km4
    # EXECUTABLE (not this lib) — libc.a references them, and executable objects
    # are always linked whereas a lib member only gets pulled if pre-referenced.
)

# The BT trace/debug source uses bare printf() — satisfied by our printf wrapper
# in hci_compat.c (-> ROM _rtl_vsnprintf + DiagPrintf), so no newlib stdio pull.
target_compile_definitions(amebad_hci PRIVATE
    CONFIG_BT=1 CONFIG_BT_EN=1 CONFIG_PLATFORM_8721D=1 ${_fw_def} ${_skip_patch_def})

target_link_libraries(amebad_hci PUBLIC
    amebad_hci_headers
    amebad_sdk_includes      # basic_types / ameba_soc / fwlib / serial_api
    amebad_osdep             # osdep_service.h
    amebad_wifi_headers      # wifi_conf.h (wifi_is_up), bt_intf.h
    amebad_ftl               # ftl_save/load_to/from_storage
    amebad_wlan              # rtlk_bt_set_gnt_bt*, rltk_bt_get_patch_code, rtw_create_secure_context
    FreeRTOS
)

# Vendor code — quiet the warnings + -fcommon (legacy Realtek tentative globals,
# e.g. rtlbt_config declared without extern in multiple TUs).
target_compile_options(amebad_hci PRIVATE
    -fcommon
    -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
    -Wno-sign-compare -Wno-unused-but-set-variable
    -Wno-implicit-function-declaration -Wno-int-conversion -Wno-discarded-qualifiers)
