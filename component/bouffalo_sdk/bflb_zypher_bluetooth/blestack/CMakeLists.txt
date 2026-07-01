# blestack — HCI transport layer only (Bouffalo's Zephyr-derived port, host removed).
#
# The Zephyr BLE host (att/gatt/l2cap/smp/conn/keys/settings + services/sbc/cli) was
# deleted; what remains is the HCI transport that sits under a host and talks to the
# prebuilt controller .a, plus the minimal Zephyr base infra it needs to compile
# (common buffer pool / atomics / workqueue / log, the FreeRTOS port shim, and the
# public bluetooth/ + driver headers). This is the bare-HCI foundation (cf.
# component/amebad/hci); a real host (e.g. NimBLE) can be layered on later.
#
# HCI transport is one of two branches:
#   BFLB_BT_HOST_HCI_TL=OFF (default) -> bl_hci_wrapper.c (on-chip controller wrapper)
#   BFLB_BT_HOST_HCI_TL=ON            -> hci_tl/* (H4 over UART)
# The hci_onchip/hci_driver.c bridge is always in.

option(BFLB_BT_HOST_HCI_TL   "Use hci_tl/* (H4 UART) instead of bl_hci_wrapper" OFF)
option(BFLB_BT_HCI_UART_INT  "Build hci_tl/bflb_hci_uart_int.c"                 OFF)

add_library(bflb_zypher_bluetooth_blestack STATIC
    # core port + common infra
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
    # tinycrypt (crypto primitives the transport/infra may reference)
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

# Public include surface — the transport + shared headers consumers get when they
# link this lib (host/services/cli include dirs dropped with those sources).
target_include_directories(bflb_zypher_bluetooth_blestack PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/port/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/include/zephyr"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/include/misc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/tinycrypt/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/hci_onchip"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/include/bluetooth"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/include/drivers/bluetooth"
)

# Deps: chip HAL for register access, FreeRTOS for the bl_port shim, config target
# for the CONFIG_BT_* macro wall, plus getopt.
target_link_libraries(bflb_zypher_bluetooth_blestack PUBLIC
    bflb_lhal
    freertos_kernel
    bflb_zypher_bluetooth_config
    bflb_getopt
)

# blestack pulls the controller's HCI API headers (ble_lib_api.h, hci_onchip.h on
# BL602; btble_lib_api.h on BL616+). Headers only — the controller is INTERFACE+.a.
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

# tinycrypt + Zephyr port trip the upstream's strict warnings. Same -w as zlib/cjson.
target_compile_options(bflb_zypher_bluetooth_blestack PRIVATE -w)
