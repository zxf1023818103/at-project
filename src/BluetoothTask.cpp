#include "BluetoothTask.h"

namespace at {

BluetoothTask::BluetoothTask(string deviceName) : LowerBluetooth(deviceName) {}

BluetoothTask::~BluetoothTask() = default;

void BluetoothTask::exit() {
    sendCallback<bool>([]() {
        return false;
    });
    waitForExit();
}

shared_ptr<MacAddress> BluetoothTask::macAddress() {
    return sendCallback<MacAddress>([this]() {
        return macAddressInternal();
    });
}

void BluetoothTask::setDeviceName(string deviceName) {
    // Stays sendCallback<void>: CallbackTask<bool> uses `false` as the
    // "exit task" signal (see BluetoothTask::exit()), so any transient
    // gaps_set_parameter / bt_set_name failure would terminate the BT
    // routine. Log failures inside setDeviceNameInternal instead.
    return sendCallback<void>([deviceName, this]() {
        setDeviceNameInternal(deviceName);
    });
}

bool BluetoothTask::stopAdvertising() {
    return sendCallback<bool>([this]() {
        return stopAdvertisingInternal();
    });
}

}  // namespace at
