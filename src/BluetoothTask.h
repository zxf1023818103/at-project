#pragma once

#include "LowerBluetoothTask.h"
#include "CallbackTask.h"
#include "MacAddress.h"
#include "BluetoothField.h"

namespace at {

struct BluetoothTask : public LowerBluetoothTask {

    explicit BluetoothTask(string deviceName = "BT_DEVICE");

    virtual ~BluetoothTask() override;

    template<typename CallbackReturnType>
    auto sendCallback(Callback<CallbackReturnType> callback) {
        auto callbackTaskPtr = make_shared<CallbackTask<CallbackReturnType>>(callback);
        LowerBluetoothTask::sendCallback(static_pointer_cast<AbstractCallbackTask>(callbackTaskPtr));
        return callbackTaskPtr->result();
    }

    void exit();

    shared_ptr<MacAddress> macAddress();

    void setDeviceName(string deviceName);

    template<typename BluetoothFieldList>
    bool startAdvertising(const struct bt_le_adv_param &param, const BluetoothFieldList &ad, const BluetoothFieldList &sd) {
        return sendCallback<bool>([param, ad, sd, this]() {
            return startAdvertisingInternal(param, begin(ad), size(ad), begin(sd), size(sd));
        });
    }

    template<typename BluetoothFieldList>
    bool updateAdvertisingData(const BluetoothFieldList &ad, const BluetoothFieldList &sd) {
        return sendCallback<bool>([ad, sd, this]() {
            return updateAdvertisingDataInternal(begin(ad), size(ad), begin(sd), size(sd));
        });
    }

    bool stopAdvertising();
};

}  // namespace at
