#pragma once

#include "Common.h"
#include "Task.h"
#include "Logger.h"
#include "MacAddress.h"
#include "BluetoothAdvReport.h"
#include "AbstractCallbackTask.h"

#if defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616 || defined BL616CL

#include "SharedPtrQueue.h"

namespace at {

class ZephyrBluetooth : public Task {
private:
    SharedPtrQueue<AbstractCallbackTask> m_callbackQueue;

public:
    ZephyrBluetooth(string deviceName);

    virtual ~ZephyrBluetooth() override;

    void sendCallback(shared_ptr<AbstractCallbackTask> callback);

protected:
    shared_ptr<MacAddress> macAddressInternal();

    void setDeviceNameInternal(string deviceName);

    template<typename BluetoothFieldIterator, typename OutputIterator>
    OutputIterator buildAdvertisingDataInternal(BluetoothFieldIterator inputBegin, size_t& remainingInputLength, OutputIterator outputBegin, size_t& remainingOutputLength) {
        auto outputIterator = outputBegin;
        auto it = inputBegin;
        while (remainingInputLength > 0 && remainingOutputLength > 0) {
            my_assert(size(it->data) < 0xFF);
            outputIterator->type = it->type;
            outputIterator->data_len = static_cast<uint8_t>(size(it->data));
            outputIterator->data = const_cast<uint8_t*>(data(it->data));
            --remainingInputLength;
            --remainingOutputLength;
            ++it;
            ++outputIterator;
        }
        return outputIterator;
    }

    template<typename BluetoothFieldIterator>
    auto buildBluetoothDataArray(BluetoothFieldIterator inputBegin, size_t inputLength) {
        shared_ptr<bt_data[]> dataArray(new bt_data[inputLength], default_delete<bt_data[]>());
        for (size_t i = 0; i < inputLength; ++i) {
            my_assert(size((inputBegin + i)->data) < 0xFF);
            dataArray[i].type = (inputBegin + i)->type;
            dataArray[i].data_len = static_cast<uint8_t>(size((inputBegin + i)->data));
            dataArray[i].data = const_cast<uint8_t*>(data((inputBegin + i)->data));
        }
        return dataArray;
    }

    template<typename AdvertisingFieldIterator, typename ScanResponseFieldIterator>
    bool startAdvertisingInternal(const struct bt_le_adv_param param, AdvertisingFieldIterator adBegin, size_t adLength, ScanResponseFieldIterator sdBegin, size_t sdLength) {
        auto ad = buildBluetoothDataArray(adBegin, adLength);
        auto sd = buildBluetoothDataArray(sdBegin, sdLength);
        return ::bt_le_adv_start(&param, ad.get(), adLength, sd.get(), sdLength) == 0;
    }

    template<typename AdvertisingFieldIterator, typename ScanResponseFieldIterator>
    bool updateAdvertisingDataInternal(AdvertisingFieldIterator adBegin, size_t adLength, ScanResponseFieldIterator sdBegin, size_t sdLength) {
        auto ad = buildBluetoothDataArray(adBegin, adLength);
        auto sd = buildBluetoothDataArray(sdBegin, sdLength);
        return ::bt_le_adv_update_data(ad.get(), adLength, sd.get(), sdLength) == 0;
    }

    bool stopAdvertisingInternal();

    void routine() override;
};

}  // namespace at

#endif // bouffalo platforms
