#pragma once

#include "AmebaBluetooth.h"
#include "ZephyrBluetooth.h"
#include "CallbackTask.h"
#include "MacAddress.h"
#include "BluetoothField.h"

namespace at {

#if defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C || defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2
using LowerBluetooth = AmebaBluetooth;
#elif defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616 || defined BL616CL
using LowerBluetooth = ZephyrBluetooth;
#endif

struct BluetoothTask : public LowerBluetooth {

    explicit BluetoothTask(string deviceName = "BT_DEVICE");

    virtual ~BluetoothTask() override;

    template<typename CallbackReturnType>
    auto sendCallback(Callback<CallbackReturnType> callback) {
        auto callbackTaskPtr = make_shared<CallbackTask<CallbackReturnType>>(callback);
        LowerBluetooth::sendCallback(static_pointer_cast<AbstractCallbackTask>(callbackTaskPtr));
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
