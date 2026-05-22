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
    ::hci_driver_init();
    ::bt_enable(nullptr);

    setDeviceNameInternal(deviceName);
}

ZephyrBluetooth::~ZephyrBluetooth() {
    ::bt_disable();
}

void ZephyrBluetooth::sendCallback(shared_ptr<AbstractCallbackTask> callback) {
    m_callbackQueue.send(callback);
}

shared_ptr<MacAddress> ZephyrBluetooth::macAddressInternal() {
    auto macPtr = make_shared<MacAddress>();
    ::bt_addr_le_t local_addr;
    my_assert(::bt_get_local_public_address(&local_addr) == 0);
    reverse_copy(local_addr.a.val, local_addr.a.val + 6, begin(macPtr->address));
    return macPtr;
}

void ZephyrBluetooth::setDeviceNameInternal(string deviceName) {
    ::bt_set_name(deviceName.c_str());
}

bool ZephyrBluetooth::stopAdvertisingInternal() {
    return ::bt_le_adv_stop() == 0;
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
