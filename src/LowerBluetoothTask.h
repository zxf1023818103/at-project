#pragma once

#include "Common.h"
#include "Task.h"
#include "Logger.h"
#include "MacAddress.h"
#include "BluetoothAdvReport.h"
#include "AbstractCallbackTask.h"

#if defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C || defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2
#include "BluetoothTaskIoQueueHolder.h"
#include "CallbackTaskSharedPtrIoQueue.h"
#endif

#if defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616
#include "SharedPtrQueue.h"
#endif

namespace at {

#if defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C || defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2

class LowerBluetoothTask : public Task {
public:
    bool stackRunning() const;

private:
    void waitForStackRunning() const;

    static ::T_APP_RESULT gapCallback(uint8_t type, void *args);

private:
    BluetoothTaskEventQueueHolder m_eventQueue;

    CallbackTaskSharedPtrIoQueue m_ioQueue;

public:
    LowerBluetoothTask(string deviceName);

    virtual ~LowerBluetoothTask() override;

private:
    void onDisconnected(uint8_t connId, uint16_t reason);

    void onConnected(uint8_t connId);

    bool handleGapMessage(::T_IO_MSG *ioMsg);

protected:
    virtual void routine() override;

    void notify();

    shared_ptr<MacAddress> macAddressInternal();

    void setDeviceNameInternal(string deviceName);

    bool stopAdvertisingInternal();

    using BluetoothAdvDataBuffer = array<uint8_t, 254>;

    using BluetoothAdvDataBufferSharedPtr = shared_ptr<BluetoothAdvDataBuffer>;

private:
    template<typename BluetoothFieldIterator, typename OutputIterator>
    static OutputIterator buildAdvertisingField(BluetoothFieldIterator input, size_t& remainingInputLength, OutputIterator outputBegin, size_t& remainingOutputLength) {
        auto outputIterator = outputBegin;
        remainingInputLength = size(input->data) + 2;
        if (remainingOutputLength > 0 && remainingInputLength > 0) {
            auto fieldLength = size(input->data) + 1;
            my_assert(fieldLength <= 0xFF);
            *outputIterator++ = static_cast<uint8_t>(fieldLength);
            --remainingOutputLength;
            --remainingInputLength;
            if (remainingOutputLength > 0 && remainingInputLength > 0) {
                *outputIterator++ = input->type;
                --remainingOutputLength;
                --remainingInputLength;
                size_t copyLength = min(remainingInputLength, remainingOutputLength);
                remainingOutputLength -= copyLength;
                remainingInputLength -= copyLength;
                outputIterator = copy_n(begin(input->data), copyLength, outputIterator);
            }
        }
        return outputIterator;
    }

    template<typename BluetoothFieldIterator, typename OutputIterator>
    OutputIterator buildAdvertisingDataInternal(BluetoothFieldIterator adBegin, size_t& remainingAdLength, OutputIterator outputBegin, size_t& remainingOutputLength) {
        auto outputIterator = outputBegin;
        auto it = adBegin;
        while (remainingAdLength > 0 && remainingOutputLength > 0) {
            size_t remainingInputLength = 0;
            outputIterator = buildAdvertisingField(it, remainingInputLength, outputIterator, remainingOutputLength);
            if (remainingInputLength > 0) {
                getLogger()->logFormat(Logger::WARN, "Advertising data field too large to fit in the remaining output buffer, remainingInputLength=%zu", remainingInputLength);
                break;
            }
            --remainingAdLength;
            ++it;
        }
        return outputIterator;
    }

    template<typename AdvertisingFieldIterator, typename ScanResponseFieldIterator>
    void setAdvertisingDataInternal(AdvertisingFieldIterator adBegin, size_t adLength, ScanResponseFieldIterator sdBegin, size_t sdLength) {
        my_assert(adLength > 0);

        BluetoothAdvDataBufferSharedPtr adOutput = make_shared<BluetoothAdvDataBuffer>();
        size_t remainingAdOutputLength = size(*adOutput);
        size_t remainingadLength = adLength;
        auto adOutputBegin = begin(*adOutput);
        auto adOutputEnd = buildAdvertisingDataInternal(adBegin, remainingadLength, adOutputBegin, remainingAdOutputLength);
        ::le_adv_set_param(GAP_PARAM_ADV_DATA, distance(adOutputBegin, adOutputEnd), data(*adOutput));

        if (sdLength > 0) {
            BluetoothAdvDataBufferSharedPtr sdOutput = make_shared<BluetoothAdvDataBuffer>();
            size_t remainingSdOutputLength = size(*sdOutput);
            size_t remainingsdLength = sdLength;
            auto sdOutputBegin = begin(*sdOutput);
            auto sdOutputEnd = buildAdvertisingDataInternal(sdBegin, remainingsdLength, sdOutputBegin, remainingSdOutputLength);
            ::le_adv_set_param(GAP_PARAM_SCAN_RSP_DATA, distance(sdOutputBegin, sdOutputEnd), data(*sdOutput));
        }
    }

protected:
    template<typename AdvertisingFieldIterator, typename ScanResponseFieldIterator>
    bool updateAdvertisingDataInternal(AdvertisingFieldIterator adBegin, size_t adLength, ScanResponseFieldIterator sdBegin, size_t sdLength) {
        setAdvertisingDataInternal(adBegin, adLength, sdBegin, sdLength);
        return ::le_adv_update_param() == GAP_CAUSE_SUCCESS;
    }

    void setAdvertisingParametersInternal(const struct bt_le_adv_param *param, bool hasScanResponseData);

    template<typename AdvertisingFieldIterator, typename ScanResponseFieldIterator>
    bool startAdvertisingInternal(const struct bt_le_adv_param param, AdvertisingFieldIterator adBegin, size_t adLength, ScanResponseFieldIterator sdBegin, size_t sdLength) {
        setAdvertisingParametersInternal(&param, sdLength ? true : false);
        setAdvertisingDataInternal(adBegin, adLength, sdBegin, sdLength);
        return ::le_adv_start() == ::GAP_CAUSE_SUCCESS;
    }

public:
    virtual void waitForStart() const override;

    void sendCallback(shared_ptr<AbstractCallbackTask> callback);
};

#endif // realtek platforms

#if defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616

class LowerBluetoothTask : public Task {
private:
    SharedPtrQueue<AbstractCallbackTask> m_callbackQueue;

public:
    LowerBluetoothTask(string deviceName);

    virtual ~LowerBluetoothTask() override;

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

#endif // bouffalo platforms

}  // namespace at
