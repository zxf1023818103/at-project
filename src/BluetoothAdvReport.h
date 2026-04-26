#pragma once

#include "Common.h"
#include "SharedPtrQueue.h"

namespace at {

struct BluetoothAdvReport {
    uint8_t addrType;
    uint8_t bdAddr[6];
    int8_t rssi;
    uint8_t advType;
    uint8_t dataLen;
    uint8_t data[1];
};

SharedPtrQueue<BluetoothAdvReport> *getAdvReportQueue();

}  // namespace at
