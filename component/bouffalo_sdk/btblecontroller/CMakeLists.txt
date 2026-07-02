# btblecontroller — INTERFACE wrapper around the BL616 / BL616CL prebuilt
# BT/BLE combo controller .a. Picked by CONFIG_BTBLECONTROLLER_LIB.
#
# Headers exported:
#   btble_inc/, btblecontroller_port/
# Linked archive:
#   lib/libbtblecontroller_${BOUFFALO_CHIP}_${CONFIG_BTBLECONTROLLER_LIB}.a
#
# The CONFIG_BT_* / CONFIG_BLE_* compile defs (formerly in the separate
# bflb_zypher_bluetooth_config target) are folded in here — bl616 uses the
# ble1m10s1bredr0 profile, bl616cl the m2s1 profile.

if(NOT ("${BOUFFALO_CHIP}" STREQUAL "bl616" OR
        "${BOUFFALO_CHIP}" STREQUAL "bl616cl"))
    message(FATAL_ERROR
        "btblecontroller: only bl616 / bl616cl supported "
        "(BL618DG .a's pruned).")
endif()

add_library(btblecontroller INTERFACE)

target_include_directories(btblecontroller INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/btble_inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/btblecontroller_port"
)

# Always-on compile defs (mirror ble_common.cmake's unconditional set).
# CONFIG_BLE_USING_DYNAMIC_RAM=0 mirrors its `sdk_ifndef(... 0)` — controller
# code uses `#if (CONFIG_BLE_USING_DYNAMIC_RAM)` unguarded, which under -Wundef
# warns when the macro is merely absent.
target_compile_definitions(btblecontroller INTERFACE
    CONFIG_BLE
    CONFIG_BLE_ENABLE
    BFLB_BLE
    CONFIG_FREERTOS
    CONFIG_BLE_USING_DYNAMIC_RAM=0
)

if("${BOUFFALO_CHIP}" STREQUAL "bl616")
    if(NOT DEFINED CONFIG_BTBLECONTROLLER_LIB)
        set(CONFIG_BTBLECONTROLLER_LIB "ble1m10s1bredr0")
    endif()
    if(NOT "${CONFIG_BTBLECONTROLLER_LIB}" STREQUAL "ble1m10s1bredr0")
        message(FATAL_ERROR
            "btblecontroller: BL616 controller profile "
            "'${CONFIG_BTBLECONTROLLER_LIB}' not yet wired "
            "(only ble1m10s1bredr0).")
    endif()
    # From ble_common_readonly.cmake: bl616 + ble1m10s1bredr0 profile.
    # CONFIG_BLE_PDS implies CONFIG_HW_SEC_ENG_DISABLE. CONFIG_EM_SIZE=32 is not
    # a compile def — the BL616 ld script hardcodes __EM_SIZE = 32K once the
    # controller symbol btble_controller_init is defined (bl616_common.ld.in),
    # which happens as soon as the prebuilt btblecontroller .a is linked below.
    target_compile_definitions(btblecontroller INTERFACE
        CONFIG_BT_CONN=10
        CONFIG_CON=10
        CONFIG_BLE_TX_BUFF_DATA=10
        CONFIG_BLE_PDS
        CONFIG_HW_SEC_ENG_DISABLE
        CONFIG_BT_ALLROLES
        CONFIG_BT_CENTRAL
        CONFIG_BT_PERIPHERAL
        CONFIG_BT_OBSERVER
        CONFIG_BT_BROADCASTER
        CONFIG_DISABLE_BT_HOST_PRIVACY
        CONFIG_DISABLE_BLE_CONTROLLER_TEST_MODE
    )
else() # bl616cl
    if(NOT DEFINED CONFIG_BTBLECONTROLLER_LIB)
        set(CONFIG_BTBLECONTROLLER_LIB "m2s1")
    endif()
    if(NOT "${CONFIG_BTBLECONTROLLER_LIB}" STREQUAL "m2s1")
        message(FATAL_ERROR
            "btblecontroller: BL616CL controller profile "
            "'${CONFIG_BTBLECONTROLLER_LIB}' not yet wired (only m2s1).")
    endif()
    # From ble_common_readonly.cmake: bl616cl + m2s1 profile. CONFIG_EM_SIZE=16
    # must be propagated to the linker on BL616CL (bl616cl_common.ld.in reads
    # __LD_CONFIG_EM_SIZE — unlike BL616 where __EM_SIZE is hardcoded to 32K).
    target_compile_definitions(btblecontroller INTERFACE
        CONFIG_BT_CONN=2
        CONFIG_CON=2
        CONFIG_BLE_TX_BUFF_DATA=4
        CONFIG_BLE_PDS
        CONFIG_HW_SEC_ENG_DISABLE
        CONFIG_BT_ALLROLES
        CONFIG_BT_CENTRAL
        CONFIG_BT_PERIPHERAL
        CONFIG_BT_OBSERVER
        CONFIG_BT_BROADCASTER
        CONFIG_DISABLE_BT_HOST_PRIVACY
        CONFIG_DISABLE_BLE_CONTROLLER_TEST_MODE
    )
    target_link_options(btblecontroller INTERFACE
        "-Wl,--defsym,__LD_CONFIG_EM_SIZE=16"
    )
endif()

set(_lib "${CMAKE_CURRENT_SOURCE_DIR}/lib/libbtblecontroller_${BOUFFALO_CHIP}_${CONFIG_BTBLECONTROLLER_LIB}.a")
if(NOT EXISTS "${_lib}")
    message(FATAL_ERROR
        "btblecontroller: missing prebuilt ${_lib}")
endif()

target_link_libraries(btblecontroller INTERFACE
    "${_lib}"
)

unset(_lib)
