# bflb_zypher_bluetooth — Bouffalo's HCI transport + chip-specific controller.
# Extracted from bouffalo_sdk/components/wireless/bluetooth/, then stripped to
# the HCI layer: the Zephyr-derived BLE host (att/gatt/l2cap/smp/conn/keys/
# settings), services, sbc audio, cli, mesh and BT-classic profiles were all
# deleted. What remains is the bare-HCI foundation (cf. component/amebad/hci) —
# a real host (e.g. NimBLE) can be layered on the transport later.
#
# Sublibs:
#   blestack/        STATIC   HCI transport (hci_onchip bridge + bl_hci_wrapper
#                             or hci_tl/H4) + the minimal Zephyr base infra
#                             (net/buf pool, atomics, workqueue, log, port shim)
#                             + the public bluetooth/ + driver headers.
#   blecontroller/   INTERFACE  Prebuilt BLE controller .a for BL602 / BL702
#                               (this fork: BL602 only). Picked by
#                               CONFIG_BLECONTROLLER_LIB (default: m1s1).
#   btblecontroller/ INTERFACE  Prebuilt BT/BLE combo controller .a for
#                               BL616 / BL616CL. Picked by
#                               CONFIG_BTBLECONTROLLER_LIB.
#
# Compile-time wiring of all the CONFIG_BT_* macros is handled by the
# bflb_zypher_bluetooth_config INTERFACE target (see config/CMakeLists.txt).
# Today only the BL602 + m1s1 controller profile is wired; BL616/BL616CL
# profiles plug into the same scaffolding but need their controller
# .a's compile-def block added.

add_subdirectory(config)
add_subdirectory(blestack)

if("${BOUFFALO_CHIP}" STREQUAL "bl602")
    add_subdirectory(blecontroller)
elseif("${BOUFFALO_CHIP}" STREQUAL "bl616" OR
       "${BOUFFALO_CHIP}" STREQUAL "bl616cl")
    add_subdirectory(btblecontroller)
else()
    message(FATAL_ERROR
        "bflb_zypher_bluetooth: unsupported BOUFFALO_CHIP '${BOUFFALO_CHIP}' "
        "(blecontroller / btblecontroller .a pruned to bl602/bl616/bl616cl).")
endif()

# Umbrella INTERFACE — consumers link this one target and get the host
# + controller + config defs in one shot.
add_library(bflb_zypher_bluetooth INTERFACE)
target_link_libraries(bflb_zypher_bluetooth INTERFACE
    bflb_zypher_bluetooth_blestack
)
if("${BOUFFALO_CHIP}" STREQUAL "bl602")
    target_link_libraries(bflb_zypher_bluetooth INTERFACE
        bflb_zypher_bluetooth_blecontroller
    )
else()
    target_link_libraries(bflb_zypher_bluetooth INTERFACE
        bflb_zypher_bluetooth_btblecontroller
    )
endif()
