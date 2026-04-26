#include "BluetoothTask.h"

namespace at {

BluetoothTask::BluetoothTask(string deviceName) : LowerBluetoothTask(deviceName) {}

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
