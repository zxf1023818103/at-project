#include "AppTask.h"
#include "Tests.h"

namespace at {

AppTask::AppTask() : Task() {}

AppTask::~AppTask() = default;

void AppTask::routine() {
    testSharedPtrQueue1();
    testSharedPtrQueue2();
    testSharedPtrQueue3();
    testSyncTask();
    testAsyncTask();
    testLogger1();
    testStandardLibrary1();
    // testExceptionHandling1();
    testBluetoothTask1();
}

}  // namespace at
