#pragma once

#include "Task.h"

namespace at {

void testAsyncTask();
void testSyncTask();
void testSharedPtrQueue1();
void testSharedPtrQueue2();
void testSharedPtrQueue3();
void testLogger1();
void testStandardLibrary1();
void testBluetoothTask1();

void testUartOutStream1();
void testUartInStream1();

class TestTask : public Task {
public:
    explicit TestTask();

    virtual ~TestTask() override;

protected:
    virtual void routine() override;
};

}  // namespace at
