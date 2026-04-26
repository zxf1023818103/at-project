#include "BluetoothAdvReport.h"

namespace at {

static SharedPtrQueue<BluetoothAdvReport> *s_advReportQueue = nullptr;

SharedPtrQueue<BluetoothAdvReport> *getAdvReportQueue() {
    if (s_advReportQueue == nullptr) {
        s_advReportQueue = new SharedPtrQueue<BluetoothAdvReport>(10);
    }
    return s_advReportQueue;
}

}  // namespace at
