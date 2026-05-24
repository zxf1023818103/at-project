# blestack — Zephyr-derived BLE host (Bouffalo's port). Extracted from
# bouffalo_sdk/components/wireless/bluetooth/blestack/.
#
# Translated from the SDK's sdk_* macros to native CMake. Defaults aim at
# the BL602 + m1s1 controller profile that proj.conf currently selects:
#
#   CONFIG_BT_CONN=1, CONFIG_BT_ALLROLES, CONFIG_BT_CENTRAL,
#   CONFIG_BT_PERIPHERAL, CONFIG_BT_RESET=0, CONFIG_BT_BREDR=0,
#   CONFIG_BT_MESH=0, CONFIG_BT_HOST_HCI_TL=0 (so bl_hci_wrapper, not hci_tl),
#   CONFIG_DBG_RUN_ON_FPGA=0 (so settings.c IS compiled),
#   CONFIG_DISABLE_BT_SMP=0 (so smp.c + keys.c ARE compiled).
#
# Option knobs match the SDK Kconfig names so a project's proj.conf can
# flip them on/off without surprises:
#
#   BFLB_BT_CONN              ON   (att/conn/gatt)
#   BFLB_BT_SMP               ON   (smp + keys)
#   BFLB_BT_SETTINGS          ON   (host/settings.c; off only on FPGA bringup)
#   BFLB_BT_HOST_HCI_TL       OFF  (off → bl_hci_wrapper; on → hci_tl/h4)
#   BFLB_BLE_TP_SERVER        ON   (services/ble_tp_svc.c — proj.conf has it)
#   BFLB_BLE_MULTI_ADV        OFF
#   BFLB_BT_AUDIO / BREDR / OAD_* / MESH / NEARBY / STACK_CLI    OFF
#
# Everything else (services BAS/SCPS/DIS/IAS/HOG/ATVV, audio iso, sbc codec,
# spp, br profile, etc.) stays off in this initial cut and gets added as
# project needs surface.

option(BFLB_BT_CONN          "Build att/conn/gatt"           ON)
option(BFLB_BT_SMP           "Build smp + keys"              ON)
option(BFLB_BT_SETTINGS      "Build host/settings.c"         ON)
option(BFLB_BT_HOST_HCI_TL   "Use hci_tl/* instead of bl_hci_wrapper" OFF)
option(BFLB_BT_HCI_UART_INT  "Build hci_tl/bflb_hci_uart_int.c" OFF)
option(BFLB_BLE_TP_SERVER    "Build services/ble_tp_svc.c"   ON)
option(BFLB_BLE_MULTI_ADV    "Build host/multi_adv.c"        OFF)
option(BFLB_BT_DEBUG_MONITOR "Build host/monitor.c"          OFF)
option(BFLB_BT_RESET         "Build host/bl_host_assist.c"   OFF)

add_library(bflb_zypher_bluetooth_blestack STATIC
    # core port + common
    src/port/bl_port.c
    src/common/atomic_c.c
    src/common/buf.c
    src/common/log.c
    src/common/poll.c
    src/common/rpa.c
    src/common/work_q.c
    src/common/utils.c
    src/common/dec.c
    src/common/dummy.c
    # tinycrypt
    src/common/tinycrypt/source/aes_decrypt.c
    src/common/tinycrypt/source/aes_encrypt.c
    src/common/tinycrypt/source/cbc_mode.c
    src/common/tinycrypt/source/ccm_mode.c
    src/common/tinycrypt/source/cmac_mode.c
    src/common/tinycrypt/source/ctr_mode.c
    src/common/tinycrypt/source/ctr_prng.c
    src/common/tinycrypt/source/ecc.c
    src/common/tinycrypt/source/ecc_dh.c
    src/common/tinycrypt/source/ecc_dsa.c
    src/common/tinycrypt/source/ecc_platform_specific.c
    src/common/tinycrypt/source/hmac.c
    src/common/tinycrypt/source/hmac_prng.c
    src/common/tinycrypt/source/sha256.c
    src/common/tinycrypt/source/utils.c
    # hci on-chip driver bridge
    src/hci_onchip/hci_driver.c
    # core host
    src/host/crypto.c
    src/host/hci_core.c
    src/host/hci_ecc.c
    src/host/l2cap.c
    src/host/uuid.c
    # component version stamp (one-up dir)
    ../version_btble_component.c
)

# HCI transport — exactly one of these branches.
if(BFLB_BT_HOST_HCI_TL)
    target_sources(bflb_zypher_bluetooth_blestack PRIVATE
        src/hci_tl/bflb_hci_tl.c
        src/hci_tl/bflb_hci_transport.c
        src/hci_tl/hci_h4.c
        src/hci_tl/bflb_hci_uart_dma.c
    )
    if(BFLB_BT_HCI_UART_INT)
        target_sources(bflb_zypher_bluetooth_blestack PRIVATE
            src/hci_tl/bflb_hci_uart_int.c
        )
        target_compile_definitions(bflb_zypher_bluetooth_blestack PUBLIC
            CONFIG_BT_HCI_UART_INT
        )
    endif()
    target_include_directories(bflb_zypher_bluetooth_blestack PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/hci_tl"
    )
    target_compile_definitions(bflb_zypher_bluetooth_blestack PUBLIC
        CONFIG_BT_HOST_HCI_TL
    )
else()
    target_sources(bflb_zypher_bluetooth_blestack PRIVATE
        src/bl_hci_wrapper/bl_hci_wrapper.c
    )
    target_include_directories(bflb_zypher_bluetooth_blestack PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/bl_hci_wrapper"
    )
endif()

if(BFLB_BT_CONN)
    target_sources(bflb_zypher_bluetooth_blestack PRIVATE
        src/host/att.c
        src/host/conn.c
        src/host/gatt.c
    )
endif()

if(BFLB_BT_SMP)
    target_sources(bflb_zypher_bluetooth_blestack PRIVATE
        src/host/smp.c
        src/host/keys.c
    )
    # struct bt_conn::{required_,}sec_level + bt_auth + half of smp.c
    # are gated on CONFIG_BT_SMP inside the public headers.
    target_compile_definitions(bflb_zypher_bluetooth_blestack PUBLIC CONFIG_BT_SMP)
endif()

if(BFLB_BT_CONN)
    # CONFIG_BT_CONN's *value* (1 / 2 / 10 / …) is per-profile and set by
    # bflb_zypher_bluetooth_config (config/CMakeLists.txt). Defining it
    # bare here too caused "CONFIG_BT_CONN redefined" on every TU under
    # bl616 / bl616cl (config target sets =10 / =2, this one sets it to 1).
    # Leave the BFLB_BT_CONN gate (selects sources) but skip the
    # -DCONFIG_BT_CONN — the profile target is authoritative.
endif()

if(BFLB_BT_SETTINGS)
    target_sources(bflb_zypher_bluetooth_blestack PRIVATE
        src/host/settings.c
    )
endif()

if(BFLB_BT_DEBUG_MONITOR)
    target_sources(bflb_zypher_bluetooth_blestack PRIVATE src/host/monitor.c)
endif()

if(BFLB_BT_RESET)
    target_sources(bflb_zypher_bluetooth_blestack PRIVATE src/host/bl_host_assist.c)
    target_compile_definitions(bflb_zypher_bluetooth_blestack PUBLIC CONFIG_BT_RESET)
endif()

if(BFLB_BLE_TP_SERVER)
    target_sources(bflb_zypher_bluetooth_blestack PRIVATE
        src/services/ble_tp_svc.c
    )
    target_compile_definitions(bflb_zypher_bluetooth_blestack PUBLIC
        CONFIG_BLE_TP_SERVER
    )
endif()

if(BFLB_BLE_MULTI_ADV)
    target_sources(bflb_zypher_bluetooth_blestack PRIVATE src/host/multi_adv.c)
    target_compile_definitions(bflb_zypher_bluetooth_blestack PUBLIC
        CONFIG_BLE_MULTI_ADV
    )
endif()

# Public include surface — what consumers (src/main.cpp, supplicants,
# etc.) get when they link this lib. Matches the SDK's
# sdk_add_include_directories block in blestack/CMakeLists.txt.
target_include_directories(bflb_zypher_bluetooth_blestack PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/port/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/include/zephyr"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/include/misc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/tinycrypt/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/hci_onchip"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/host"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/include/bluetooth"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/include/drivers/bluetooth"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/profiles"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cli_cmds"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/services"
)

# Deps: chip HAL for register access, FreeRTOS for the bl_port shim,
# config target for the CONFIG_BT_* macro wall, plus the controller
# headers (ble_lib_api / btble_lib_api) — chip-conditional, wired in
# the umbrella.
target_link_libraries(bflb_zypher_bluetooth_blestack PUBLIC
    bflb_lhal
    freertos_kernel
    bflb_zypher_bluetooth_config
    bflb_getopt
)

# blestack pulls the controller's HCI API headers (ble_lib_api.h,
# hci_onchip.h on BL602; btble_lib_api.h on BL616+). Wire them as direct
# include paths — using target_link_libraries on the controller would
# work too but the controller is INTERFACE+prebuilt.a; we only want its
# headers at compile time, not its symbols pulled into blestack's
# static archive.
if("${BOUFFALO_CHIP}" STREQUAL "bl602")
    target_include_directories(bflb_zypher_bluetooth_blestack PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../blecontroller/ble_inc"
    )
elseif("${BOUFFALO_CHIP}" STREQUAL "bl616" OR
       "${BOUFFALO_CHIP}" STREQUAL "bl616cl")
    target_include_directories(bflb_zypher_bluetooth_blestack PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../btblecontroller/btble_inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/../btblecontroller/btblecontroller_port"
    )
endif()

# tinycrypt + Zephyr port use trig + pull stuff that the upstream's strict
# warnings catch. Same -w treatment as zlib / cjson upstream sources.
target_compile_options(bflb_zypher_bluetooth_blestack PRIVATE -w)
