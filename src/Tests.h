#pragma once

namespace at {

void testAsyncTask();
void testSyncTask();
void testSharedPtrQueue1();
void testSharedPtrQueue2();
void testSharedPtrQueue3();
void testLogger1();
void testStandardLibrary1();
void testBluetoothTask1();

#if defined BL602 || defined BL702
void testBl602UartOutStream1();
#endif // defined BL602 || defined BL702

}  // namespace at
