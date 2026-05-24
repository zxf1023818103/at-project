# bflb_zypher_bluetooth_config — INTERFACE target that propagates the
# compile defs the BT stack expects. The original SDK does this via
# ble_common.cmake + ble_common_readonly.cmake (~2000 lines of
# Makefile-derived conditionals); this file distills the subset
# actually needed for the controller profiles we support.
#
# Profiles wired today:
#   bl602 / CONFIG_BLECONTROLLER_LIB=m1s1  (single connection, central+peripheral)
#
# Add more profiles by extending the chip+lib if/elseif chain. The
# corresponding _check_conflict block in
# bouffalo_sdk/components/wireless/bluetooth/ble_common_readonly.cmake
# is the source of truth for each profile's CONFIG_BT_* values.

add_library(bflb_zypher_bluetooth_config INTERFACE)

# Always-on for any BLE build (mirrors ble_common.cmake's unconditional
# defs).
target_compile_definitions(bflb_zypher_bluetooth_config INTERFACE
    CONFIG_BLE
    CONFIG_BLE_ENABLE
    BFLB_BLE
    CONFIG_FREERTOS
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
elseif("${BOUFFALO_CHIP}" STREQUAL "bl616" OR
       "${BOUFFALO_CHIP}" STREQUAL "bl616cl")
    message(FATAL_ERROR
        "bflb_zypher_bluetooth: ${BOUFFALO_CHIP} profile-defs not wired yet "
        "(add the matching _check_conflict block from "
        "ble_common_readonly.cmake).")
endif()
