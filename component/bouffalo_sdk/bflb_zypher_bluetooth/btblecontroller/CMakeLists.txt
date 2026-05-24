# btblecontroller — INTERFACE wrapper around the BL616 / BL616CL prebuilt
# BT/BLE combo controller .a. Picked by CONFIG_BTBLECONTROLLER_LIB.
#
# Headers exported:
#   btble_inc/, btblecontroller_port/
# Linked archive:
#   lib/libbtblecontroller_${BOUFFALO_CHIP}_${CONFIG_BTBLECONTROLLER_LIB}.a

if(NOT ("${BOUFFALO_CHIP}" STREQUAL "bl616" OR
        "${BOUFFALO_CHIP}" STREQUAL "bl616cl"))
    message(FATAL_ERROR
        "btblecontroller: only bl616 / bl616cl supported "
        "(BL618DG .a's pruned).")
endif()

if(NOT DEFINED CONFIG_BTBLECONTROLLER_LIB)
    message(FATAL_ERROR
        "btblecontroller: CONFIG_BTBLECONTROLLER_LIB not set — must match "
        "one of the prebuilt lib/libbtblecontroller_${BOUFFALO_CHIP}_*.a "
        "variants (e.g. ble1m10s1bredr0, m2s1, ble1m2s1bredr1, "
        "bleuarthci, uarthci, m0b1).")
endif()

set(_lib "${CMAKE_CURRENT_SOURCE_DIR}/lib/libbtblecontroller_${BOUFFALO_CHIP}_${CONFIG_BTBLECONTROLLER_LIB}.a")
if(NOT EXISTS "${_lib}")
    message(FATAL_ERROR
        "btblecontroller: missing prebuilt ${_lib}")
endif()

add_library(bflb_zypher_bluetooth_btblecontroller INTERFACE)

target_include_directories(bflb_zypher_bluetooth_btblecontroller INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/btble_inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/btblecontroller_port"
)

target_link_libraries(bflb_zypher_bluetooth_btblecontroller INTERFACE
    "${_lib}"
    bflb_zypher_bluetooth_config
)

unset(_lib)
