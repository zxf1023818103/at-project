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

namespace at {

class AmebaBluetooth : public Task {
public:
    bool stackRunning() const;

private:
    void waitForStackRunning() const;

    static ::T_APP_RESULT gapCallback(uint8_t type, void *args);

private:
    BluetoothTaskEventQueueHolder m_eventQueue;

    CallbackTaskSharedPtrIoQueue m_ioQueue;

public:
    AmebaBluetooth(string deviceName);

    virtual ~AmebaBluetooth() override;

private:
    void onDisconnected(uint8_t connId, uint16_t reason);

    void onConnected(uint8_t connId);

    bool handleGapMessage(::T_IO_MSG *ioMsg);

protected:
    virtual void routine() override;

    void notify();

    shared_ptr<MacAddress> macAddressInternal();

    bool setDeviceNameInternal(string deviceName);

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
        size_t remainingAdLength = adLength;
        auto adOutputBegin = begin(*adOutput);
        auto adOutputEnd = buildAdvertisingDataInternal(adBegin, remainingAdLength, adOutputBegin, remainingAdOutputLength);
        ::le_adv_set_param(GAP_PARAM_ADV_DATA, distance(adOutputBegin, adOutputEnd), data(*adOutput));

        if (sdLength > 0) {
            BluetoothAdvDataBufferSharedPtr sdOutput = make_shared<BluetoothAdvDataBuffer>();
            size_t remainingSdOutputLength = size(*sdOutput);
            size_t remainingSdLength = sdLength;
            auto sdOutputBegin = begin(*sdOutput);
            auto sdOutputEnd = buildAdvertisingDataInternal(sdBegin, remainingSdLength, sdOutputBegin, remainingSdOutputLength);
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

}  // namespace at

#endif // realtek platforms
