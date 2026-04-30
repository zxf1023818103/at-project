#include "AppTask.h"
#include "Tests.h"

namespace at {

AppTask::AppTask() : Task() {}

AppTask::~AppTask() = default;

void AppTask::routine() {
    // testSharedPtrQueue1();
    // testSharedPtrQueue2();
    // testSharedPtrQueue3();
    // testSyncTask();
    // testAsyncTask();
    // testLogger1();
    // testStandardLibrary1();
    // testExceptionHandling1();
    // testBluetoothTask1();
#if defined BL602 || defined BL702
    testBl602UartOutStream1();
    testBl602UartInStream1();
#endif // defined BL602 || defined BL702
}

}  // namespace at
