# bflb_zypher_bluetooth_config — INTERFACE target that propagates the
# compile defs the BT stack expects. The original SDK does this via
# ble_common.cmake + ble_common_readonly.cmake (~2000 lines of
# Makefile-derived conditionals); this file distills the subset
# actually needed for the controller profiles we support.
#
# Profiles wired today:
#   bl602   / CONFIG_BLECONTROLLER_LIB=m1s1               (1 conn, central+peripheral)
#   bl616   / CONFIG_BTBLECONTROLLER_LIB=ble1m10s1bredr0  (10 conn, BLE-only, all roles)
#   bl616cl / CONFIG_BTBLECONTROLLER_LIB=m2s1             (2 conn, all roles, EM=16K)
#
# Add more profiles by extending the chip+lib if/elseif chain. The
# corresponding _check_conflict block in
# bouffalo_sdk/components/wireless/bluetooth/ble_common_readonly.cmake
# is the source of truth for each profile's CONFIG_BT_* values.

add_library(bflb_zypher_bluetooth_config INTERFACE)

# Always-on for any BLE build (mirrors ble_common.cmake's unconditional
# defs). CONFIG_BLE_USING_DYNAMIC_RAM mirrors upstream
# ble_common.cmake's `sdk_ifndef(CONFIG_BLE_USING_DYNAMIC_RAM 0)` — needed
# because blestack code uses `#if (CONFIG_BLE_USING_DYNAMIC_RAM)`
# unguarded, which under -Wundef warns when the macro is just absent.
target_compile_definitions(bflb_zypher_bluetooth_config INTERFACE
    CONFIG_BLE
    CONFIG_BLE_ENABLE
    BFLB_BLE
    CONFIG_FREERTOS
    CONFIG_BLE_USING_DYNAMIC_RAM=0
)
# Note on BL_MCU_SDK: bl_port.c does ship a Sec_Eng_Trng-based bl_rand
# under #if defined(BL_MCU_SDK), which looks like the obvious switch to
# avoid the weak stub in bflb_lhal. It is NOT — BL_MCU_SDK is a
# completely separate Bouffalo SDK (bouffalolab/bl_mcu_sdk), and
# defining it in this BOUFFALO_SDK-flavoured tree:
#   (a) flips bl_port.h to #include "misc.h" instead of "misc/util.h"
#       (bl_mcu_sdk vs bouffalo_sdk header layouts disagree); and
#   (b) needs Sec_Eng_Trng_Get_Random / Sec_Eng_Trng_Enable, which are
#       only declared (extern) in bl_port.c — no definition exists in
#       this fork of bouffalo_sdk (drivers, prebuilt .a, components, or
#       examples). Greppable. They presumably live in bl_mcu_sdk's
#       drivers/soc/bl602/std/ which this fork doesn't ship.
# Keeping the bflb_lhal/src/bl_rand_stub.c (weak, rand()-backed) fallback.

if("${BOUFFALO_CHIP}" STREQUAL "bl602")
    if(NOT DEFINED CONFIG_BLECONTROLLER_LIB)
        set(CONFIG_BLECONTROLLER_LIB "m1s1")
    endif()
    target_compile_definitions(bflb_zypher_bluetooth_config INTERFACE
        CONFIG_SET_TX_PWR
    )
    if("${CONFIG_BLECONTROLLER_LIB}" STREQUAL "m1s1")
        # From ble_common_readonly.cmake: bl602 + m1s1 profile.
        # CONFIG_BT_OAD_SERVER is part of the SDK's m1s1 profile but is
        # intentionally OFF here — the project doesn't use OAD and turning
        # it on would pull oad_main / oad_service / OAD_C glue we
        # haven't extracted.
        target_compile_definitions(bflb_zypher_bluetooth_config INTERFACE
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
    else()
        message(FATAL_ERROR
            "bflb_zypher_bluetooth: BL602 controller profile "
            "'${CONFIG_BLECONTROLLER_LIB}' not yet wired (only m1s1).")
    endif()
elseif("${BOUFFALO_CHIP}" STREQUAL "bl616")
    if(NOT DEFINED CONFIG_BTBLECONTROLLER_LIB)
        set(CONFIG_BTBLECONTROLLER_LIB "ble1m10s1bredr0")
    endif()
    if("${CONFIG_BTBLECONTROLLER_LIB}" STREQUAL "ble1m10s1bredr0")
        # From ble_common_readonly.cmake: bl616 + ble1m10s1bredr0 profile.
        # CONFIG_BLE_PDS implies CONFIG_HW_SEC_ENG_DISABLE (see
        # ble_common.cmake's sdk_add_compile_definitions_ifdef line).
        # CONFIG_EM_SIZE=32 is not propagated as a compile def — the
        # BL616 ld script hardcodes __EM_SIZE = 32K when the controller
        # symbol btble_controller_init is defined (see
        # bl616_common.ld.in), which happens once we link the prebuilt
        # btblecontroller .a below.
        target_compile_definitions(bflb_zypher_bluetooth_config INTERFACE
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
    else()
        message(FATAL_ERROR
            "bflb_zypher_bluetooth: BL616 controller profile "
            "'${CONFIG_BTBLECONTROLLER_LIB}' not yet wired "
            "(only ble1m10s1bredr0).")
    endif()
elseif("${BOUFFALO_CHIP}" STREQUAL "bl616cl")
    if(NOT DEFINED CONFIG_BTBLECONTROLLER_LIB)
        set(CONFIG_BTBLECONTROLLER_LIB "m2s1")
    endif()
    if("${CONFIG_BTBLECONTROLLER_LIB}" STREQUAL "m2s1")
        # From ble_common_readonly.cmake: bl616cl + m2s1 profile.
        # CONFIG_EM_SIZE=16 must be propagated to the linker on BL616CL
        # (bl616cl_common.ld.in reads __LD_CONFIG_EM_SIZE — unlike
        # BL616 where __EM_SIZE is hardcoded to 32K based on a controller
        # symbol presence).
        target_compile_definitions(bflb_zypher_bluetooth_config INTERFACE
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
        target_link_options(bflb_zypher_bluetooth_config INTERFACE
            "-Wl,--defsym,__LD_CONFIG_EM_SIZE=16"
        )
    else()
        message(FATAL_ERROR
            "bflb_zypher_bluetooth: BL616CL controller profile "
            "'${CONFIG_BTBLECONTROLLER_LIB}' not yet wired "
            "(only m2s1).")
    endif()
endif()
