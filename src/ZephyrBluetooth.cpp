#include "ZephyrBluetooth.h"

#if defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616 || defined BL616CL

namespace at {

ZephyrBluetooth::ZephyrBluetooth(string deviceName) : Task(), m_callbackQueue(API_TASK_IO_MSG_QUEUE_SIZE) {
#if defined BL602 || defined BL702
    ::ble_controller_init(configMAX_PRIORITIES - 1);
#endif // defined BL602 || defined BL702
#if defined BL606P || defined BL808 || defined BL616 || defined BL616CL
    ::btble_controller_init(configMAX_PRIORITIES - 1);
#endif // defined BL606P || defined BL808 || defined BL616 || defined BL616CL
    // HCI-only build: the Zephyr BLE host was removed from bflb_zypher_bluetooth
    // (only the HCI transport + controller remain). hci_driver_init() wires the HCI
    // transport up into the host receive path (bt_recv/bt_hci_driver_register), which
    // no longer exists, so it's dropped along with bt_enable(); only the controller is
    // brought up. A real host (e.g. NimBLE) would re-wire the transport later.
    // ::hci_driver_init();
    // ::bt_enable(nullptr);

    setDeviceNameInternal(deviceName);
}

ZephyrBluetooth::~ZephyrBluetooth() {
    // HCI-only: no host to disable. ::bt_disable();
}

void ZephyrBluetooth::sendCallback(shared_ptr<AbstractCallbackTask> callback) {
    m_callbackQueue.send(callback);
}

shared_ptr<MacAddress> ZephyrBluetooth::macAddressInternal() {
    auto macPtr = make_shared<MacAddress>();
    // HCI-only: bt_get_local_public_address() lived in the (removed) host. Return a
    // zero address until a host is layered back on.
    ::bt_addr_le_t local_addr = {};
    reverse_copy(local_addr.a.val, local_addr.a.val + 6, begin(macPtr->address));
    return macPtr;
}

void ZephyrBluetooth::setDeviceNameInternal(string deviceName) {
    // HCI-only: bt_set_name() lived in the (removed) host — no-op.
    (void)deviceName;
}

bool ZephyrBluetooth::stopAdvertisingInternal() {
    // HCI-only: bt_le_adv_stop() lived in the (removed) host — nothing to stop.
    return true;
}

void ZephyrBluetooth::routine() {
    for (;;) {
        auto callbackTaskPtr = m_callbackQueue.receive();
        my_assert(callbackTaskPtr != nullptr);
        callbackTaskPtr->startSync();
        if (!callbackTaskPtr->success()) {
            break;
        }
    }
}

}  // namespace at

#endif // bouffalo platforms
