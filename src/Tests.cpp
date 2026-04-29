#include "Tests.h"
#include "Common.h"
#include "HelloWorldTask.h"
#include "SharedPtrQueue.h"
#include "SharedPtrQueueTestItem.h"
#include "Logger.h"
#include "BluetoothTask.h"
#include "BluetoothField.h"
#include "MacAddress.h"
#if defined BL602 || defined BL702
#include "Bl602UartStreamBuf.h"
extern "C" {
#include <bl_uart.h>
}
#endif // defined BL602 || defined BL702

namespace at {

void testAsyncTask() {
    printf("%s\n", "========= Async Task Test Started =========");
    {
        auto a = make_shared<HelloWorldTask>();
        printf("%s\n", "startAsync() called");
        a->startAsync("HelloWorldTask");
        printf("startAsync() returned\n");
        printf("%s\n", "waitForStart() called");
        a->waitForStart();
        printf("waitForStart() returned\n");
        printf("%s\n", "startAsync() called again");
        a->startAsync("HelloWorldTask");
        printf("startAsync() returned again\n");
        printf("%s\n", "waitForExit(1000) called");
        bool result = a->waitForExit(1000);
        printf("waitForExit(1000) returned %s\n", result ? "true" : "false");
        printf("%s\n", "waitForExit() called");
        a->waitForExit();
        printf("waitForExit() returned\n");
    }
    printf("%s\n\n", "========= Async Task Test Finished =========");
}

void testSyncTask() {
    printf("%s\n", "========= Sync Task Test Started =========");
    {
        auto a = make_shared<HelloWorldTask>();
        printf("%s\n", "startSync() called");
        a->startSync();
        printf("startSync() returned\n");
        printf("%s\n", "startSync() called again");
        a->startSync();
        printf("startSync() returned again\n");
        printf("%s\n", "waitForExit() called");
        a->waitForExit();
        printf("waitForExit() returned\n");
    }
    printf("%s\n\n\n", "========= Sync Task Test Finished =========");
}

void testSharedPtrQueue1() {
    printf("\n%s\n", "========= SharedPtrQueue Test 1 Started =========");
    {
        SharedPtrQueue<SharedPtrQueueTestItem> queue(3);
        for (int i = 1; i <= 5; ++i) {
            auto item = make_shared<SharedPtrQueueTestItem>(i);
            printf("Sending item with value %d\n", item->value);
            if (queue.send(item, 1000)) {
                printf("Sent item with value %d\n", item->value);
            } else {
                printf("Failed to send item with value %d\n", item->value);
            }
        }
        for (int i = 1; i <= 5; ++i) {
            printf("Receiving item...\n");
            auto item = queue.receive(1000);
            if (item != nullptr) {
                printf("Received item with value %d\n", item->value);
            } else {
                printf("Failed to receive item\n");
            }
        }
    }
    printf("\n%s\n", "========= SharedPtrQueue Test 1 Finished =========");
}

void testSharedPtrQueue2() {
    printf("\n%s\n", "========= SharedPtrQueue Test 2 Started =========");
    {
        SharedPtrQueue<SharedPtrQueueTestItem> queue(5);
        for (int i = 1; i <= 5; ++i) {
            auto item = make_shared<SharedPtrQueueTestItem>(i);
            printf("Sending item with value %d\n", item->value);
            if (queue.send(item)) {
                printf("Sent item with value %d\n", item->value);
            } else {
                printf("Failed to send item with value %d\n", item->value);
            }
        }
    }
    printf("\n%s\n", "========= SharedPtrQueue Test 2 Finished =========");
}

void testSharedPtrQueue3() {
    printf("\n%s\n", "========= SharedPtrQueue Test 3 Started =========");
    {
        SharedPtrQueue<char[]> queue(5);
        for (int i = 1; i <= 5; ++i) {
            shared_ptr<char[]> item(new char[12], [](char *p) { delete[] p; });
            memcpy(item.get(), "Hello World", 12);
            printf("Sending item with value %s\n", item.get());
            if (queue.send(item)) {
                printf("%s\n", item.get());
            } else {
                printf("Failed to send item with value %s\n", item.get());
            }
        }
        for (;;) {
            auto item = queue.receive(0);
            if (item) {
                printf("%s\n", item.get());
            }
            else {
                break;
            }
        }
    }
    printf("\n%s\n", "========= SharedPtrQueue Test 3 Finished =========");
}

void testLogger1() {
    printf("\n%s\n", "========= Logger Test 1 Started =========");
    {
        auto logger = getLogger();
        for (int i = 0; i < 4; i++) {
            Logger::Level level = static_cast<Logger::Level>(i);
            logger->setLevel(level);
            printf("\nSet Log Level %s\n", Logger::levelString(level));
            logger->logFormat(Logger::DEBUG, "Message %s", "Debug");
            logger->logFormat(Logger::INFO, "Message %s", "Info");
            logger->logFormat(Logger::WARN, "Message %s", "Warn");
            logger->logFormat(Logger::ERROR, "Message %s", "Error");
        }
    }
    printf("\n%s\n", "========= Logger Test 1 Finished =========");
}

void testStandardLibrary1() {
    getLogger()->setLevel(Logger::DEBUG);
    getLogger()->logFormat(Logger::INFO, "========= Standard Library Test 1 Started =========");
    {
        vector<int> vec = {1, 2, 3, 4, 5};
        getLogger()->logFormat(Logger::INFO, "Vector contents:");
        for (const auto &item : vec) {
            getLogger()->logFormat(Logger::INFO, "Item: %d", item);
        }

        map<string, int> myMap = {{"one", 1}, {"two", 2}, {"three", 3}};
        getLogger()->logFormat(Logger::INFO, "Map contents:");
        for (const auto &pair : myMap) {
            getLogger()->logFormat(Logger::INFO, "Key: %s, Value: %d", pair.first.c_str(), pair.second);
        }
    }
    getLogger()->logFormat(Logger::INFO, "========= Standard Library Test 1 Finished =========");
}

void testBluetoothTask1() {
    getLogger()->setLevel(Logger::DEBUG);
    getLogger()->logFormat(Logger::INFO, "========= Bluetooth Task Test 1 Started =========");
    {
        auto a = make_shared<BluetoothTask>();
        getLogger()->logFormat(Logger::INFO, "startAsync() called");
        a->startAsync("BluetoothTask", API_TASK_STACK_SIZE, API_TASK_PRIORITY);
        getLogger()->logFormat(Logger::INFO, "startAsync() returned");
        a->waitForStart();
        getLogger()->logFormat(Logger::INFO, "Get MAC address...");
        auto macPtr = a->macAddress();
        getLogger()->logFormat(Logger::INFO, "MAC address: %s", macPtr->toString().c_str());
        string deviceName = "BLUFI_DEVICE_" + macPtr->toHex().substr(6);
        getLogger()->logFormat(Logger::INFO, "Setting device name to: %s", deviceName.c_str());
        a->setDeviceName(deviceName);
        const uint16_t BLUFI_UUID_SRV = 0xFFFF;

        BluetoothFieldList<> ad1 = {
            {
                .type = BT_DATA_FLAGS,
                .data = { BT_LE_AD_LIMITED | BT_LE_AD_NO_BREDR },
            },
            {
                .type = BT_DATA_UUID16_ALL,
                .data = { LO_WORD(BLUFI_UUID_SRV), HI_WORD(BLUFI_UUID_SRV) },
            },
            {
                .type = BT_DATA_NAME_COMPLETE,
                .data = { deviceName.begin(), deviceName.end() },
            },
        };

        BluetoothFieldList<> sd1 = {
            {
                .type = BT_DATA_GAP_APPEARANCE,
                .data = { 0, 0 },
            },
            {
                .type = BT_DATA_MANUFACTURER_DATA,
                .data = { macPtr->address.begin(), macPtr->address.end() },
            },
        };

        struct bt_le_adv_param params = {
            .options = BT_LE_ADV_OPT_CONNECTABLE,
            .interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
            .interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
        };

        getLogger()->logFormat(Logger::INFO, "Starting advertising...");
        printBluetoothFieldList("ad1", begin(ad1), size(ad1));
        printBluetoothFieldList("sd1", begin(sd1), size(sd1));

        bool ret = a->startAdvertising(params, ad1, sd1);
        getLogger()->logFormat(Logger::INFO, "startAdvertising() returned %s", ret ? "true" : "false");
        ::vTaskDelay(pdMS_TO_TICKS(5000));

        string newDeviceName = "BLUFI_DEVICE_UPD";
        BluetoothFieldList<> ad2 = {
            {
                .type = BT_DATA_FLAGS,
                .data = { BT_LE_AD_LIMITED | BT_LE_AD_NO_BREDR },
            },
            {
                .type = BT_DATA_UUID16_ALL,
                .data = { LO_WORD(BLUFI_UUID_SRV), HI_WORD(BLUFI_UUID_SRV) },
            },
            {
                .type = BT_DATA_NAME_COMPLETE,
                .data = { newDeviceName.begin(), newDeviceName.end() },
            },
        };

        array<uint8_t, 20> customData;
        fill(begin(customData), end(customData), 0xAB);
        BluetoothFieldList<> sd2 = {
            {
                .type = BT_DATA_GAP_APPEARANCE,
                .data = { 0, 0 },
            },
            {
                .type = BT_DATA_MANUFACTURER_DATA,
                .data = { customData.begin(), customData.end() },
            },
        };

        printBluetoothFieldList("ad2", begin(ad2), size(ad2));
        printBluetoothFieldList("sd2", begin(sd2), size(sd2));

        {
            getLogger()->logFormat(Logger::INFO, "Setting new device name to: %s", newDeviceName.c_str());
            a->setDeviceName(newDeviceName);
            getLogger()->logFormat(Logger::INFO, "Updating advertising data...");
            ret = a->updateAdvertisingData(ad2, sd2);
            getLogger()->logFormat(Logger::INFO, "updateAdvertisingData() returned %s", ret ? "true" : "false");
            ::vTaskDelay(pdMS_TO_TICKS(20 * 1000));

            getLogger()->logFormat(Logger::INFO, "Reverting device name to: %s", deviceName.c_str());
            a->setDeviceName(deviceName);
            getLogger()->logFormat(Logger::INFO, "Updating advertising data...");
            ret = a->updateAdvertisingData(ad1, sd1);
            getLogger()->logFormat(Logger::INFO, "updateAdvertisingData() returned %s", ret ? "true" : "false");
            ::vTaskDelay(pdMS_TO_TICKS(20 * 1000));
        }

        getLogger()->logFormat(Logger::INFO, "Stopping advertising...");
        ret = a->stopAdvertising();
        getLogger()->logFormat(Logger::INFO, "stopAdvertising() returned %s", ret ? "true" : "false");
        getLogger()->logFormat(Logger::INFO, "Exiting Bluetooth task...");
        a->exit();
    }
    getLogger()->logFormat(Logger::INFO, "========= Bluetooth Task Test 1 Finished =========");
}

#if defined BL602 || defined BL702
void testBl602UartOutStream1() {
    constexpr uint8_t  kUartId = 1;
    constexpr uint8_t  kTxPin  = 4;
    constexpr uint8_t  kRxPin  = 3;
    constexpr uint32_t kBaud   = 115200;

    printf("\n%s\n", "========= Bl602UartOutStream Test 1 Started =========");
    {
        bl_uart_init(kUartId, kTxPin, kRxPin, 255, 255, kBaud);
        bl_uart_int_enable(kUartId);

        Bl602UartOutStream uart(kUartId);
        uart << "Hello from Bl602UartOutStream\r\n";
        uart << "decimal=" << 12345 << " hex=0x" << hex << setw(4) << setfill('0') << 0xDEAD << "\r\n";
        uart.flush();

        for (int i = 0; i < 16; ++i) {
            uart << "line " << i << " padded=" << setw(3) << setfill(' ') << (i * 7) << "\r\n";
        }
        uart.flush();

        string burst(200, 'A');
        uart << "burst: " << burst << "\r\n";
        uart.flush();

        ::vTaskDelay(pdMS_TO_TICKS(500));
    }
    printf("\n%s\n", "========= Bl602UartOutStream Test 1 Finished =========");
}
#endif // defined BL602 || defined BL702

}  // namespace at
