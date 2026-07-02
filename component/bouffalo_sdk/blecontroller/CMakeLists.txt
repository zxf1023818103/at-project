# blecontroller — INTERFACE wrapper around the BL602 (or BL702) prebuilt
# BLE controller .a. Picked by CONFIG_BLECONTROLLER_LIB (default m1s1).
#
# Headers exported:
#   ble_inc/ble_lib_api.h, ble_inc/hci_onchip.h
# Linked archive:
#   lib/libblecontroller_${BOUFFALO_CHIP}_${CONFIG_BLECONTROLLER_LIB}.a
#
# The CONFIG_BT_* / CONFIG_BLE_* compile defs (formerly in the separate
# bflb_zypher_bluetooth_config target) are folded in here — this controller is
# BL602-only, so it just carries the bl602 + m1s1 profile.

if(NOT "${BOUFFALO_CHIP}" STREQUAL "bl602")
    message(FATAL_ERROR
        "blecontroller: this fork prunes BL702 .a's — only bl602 supported.")
endif()

if(NOT DEFINED CONFIG_BLECONTROLLER_LIB)
    set(CONFIG_BLECONTROLLER_LIB "m1s1")
endif()

if(NOT "${CONFIG_BLECONTROLLER_LIB}" STREQUAL "m1s1")
    message(FATAL_ERROR
        "blecontroller: BL602 controller profile "
        "'${CONFIG_BLECONTROLLER_LIB}' not yet wired (only m1s1).")
endif()

set(_lib "${CMAKE_CURRENT_SOURCE_DIR}/lib/libblecontroller_${BOUFFALO_CHIP}_${CONFIG_BLECONTROLLER_LIB}.a")
if(NOT EXISTS "${_lib}")
    message(FATAL_ERROR
        "blecontroller: missing prebuilt ${_lib} (CHIP=${BOUFFALO_CHIP}, LIB=${CONFIG_BLECONTROLLER_LIB})")
endif()

add_library(blecontroller INTERFACE)

target_include_directories(blecontroller INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/ble_inc"
)

# Compile defs the controller expects (folded in from the old *_config target).
# Always-on set mirrors upstream ble_common.cmake's unconditional defs;
# CONFIG_BLE_USING_DYNAMIC_RAM=0 mirrors its `sdk_ifndef(... 0)` — controller
# code uses `#if (CONFIG_BLE_USING_DYNAMIC_RAM)` unguarded, which under -Wundef
# warns when the macro is merely absent. The bl602 + m1s1 block is from
# ble_common_readonly.cmake; CONFIG_BT_OAD_SERVER is part of the SDK's m1s1
# profile but intentionally OFF (no OAD glue extracted).
target_compile_definitions(blecontroller INTERFACE
    CONFIG_BLE
    CONFIG_BLE_ENABLE
    BFLB_BLE
    CONFIG_FREERTOS
    CONFIG_BLE_USING_DYNAMIC_RAM=0
    CONFIG_SET_TX_PWR
    CONFIG_BT_CONN=1
    CONFIG_BLE_TX_BUFF_DATA=2
    CONFIG_BT_ALLROLES
    CONFIG_BT_CENTRAL
    CONFIG_BT_PERIPHERAL
    CONFIG_BT_OBSERVER
    CONFIG_BT_BROADCASTER
    CONFIG_DISABLE_BT_HOST_PRIVACY
    CONFIG_DISABLE_BLE_CONTROLLER_PHY_UPDATE
    CONFIG_DISABLE_BLE_CONTROLLER_TEST_MODE
    CONFIG_CON=1
)

target_link_libraries(blecontroller INTERFACE
    "${_lib}"
)

unset(_lib)
