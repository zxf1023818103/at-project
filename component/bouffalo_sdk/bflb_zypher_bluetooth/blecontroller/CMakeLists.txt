# blecontroller — INTERFACE wrapper around the BL602 (or BL702) prebuilt
# BLE controller .a. Picked by CONFIG_BLECONTROLLER_LIB (default m1s1).
#
# Headers exported:
#   ble_inc/ble_lib_api.h, ble_inc/hci_onchip.h
# Linked archive:
#   lib/libblecontroller_${BOUFFALO_CHIP}_${CONFIG_BLECONTROLLER_LIB}.a

if(NOT "${BOUFFALO_CHIP}" STREQUAL "bl602")
    message(FATAL_ERROR
        "blecontroller: this fork prunes BL702 .a's — only bl602 supported.")
endif()

if(NOT DEFINED CONFIG_BLECONTROLLER_LIB)
    set(CONFIG_BLECONTROLLER_LIB "m1s1")
endif()

set(_lib "${CMAKE_CURRENT_SOURCE_DIR}/lib/libblecontroller_${BOUFFALO_CHIP}_${CONFIG_BLECONTROLLER_LIB}.a")
if(NOT EXISTS "${_lib}")
    message(FATAL_ERROR
        "blecontroller: missing prebuilt ${_lib} (CHIP=${BOUFFALO_CHIP}, LIB=${CONFIG_BLECONTROLLER_LIB})")
endif()

add_library(bflb_zypher_bluetooth_blecontroller INTERFACE)

target_include_directories(bflb_zypher_bluetooth_blecontroller INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/ble_inc"
)

target_link_libraries(bflb_zypher_bluetooth_blecontroller INTERFACE
    "${_lib}"
    bflb_zypher_bluetooth_config
)

unset(_lib)
