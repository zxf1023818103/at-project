#if defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C
#define STD_PRINTF
#endif // defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C

extern "C" {

#include <FreeRTOS.h>
#include <string.h>
#include <task.h>
#include <semphr.h>

#include <lwip/tcpip.h>

#if defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C || defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2
#include <os_sched.h>
#include <os_msg.h>
#include <os_task.h>
#include <app_msg.h>
#include <gap.h>
#include <gap_adv.h>
#include <gap_scan.h>
#include <gap_bond_le.h>
#include <gap_msg.h>
#include <gap_config.h>
#include <gap_conn_le.h>
#include <bte.h>
#include <profile_server.h>
#include <gatt_builtin_services.h>
#include <rtk_coex.h>
#include <trace_app.h>
#endif // defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C || defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2

#if defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C
#include <wifi_constants.h>
#include <wifi/wifi_conf.h>
#include <wlan_fast_connect/example_wlan_fast_connect.h>

#define API_TASK_STACK_SIZE 256*12
#define API_TASK_PRIORITY 4
#define API_TASK_IO_MSG_QUEUE_SIZE 0x20
#define API_TASK_GAP_MSG_QUEUE_SIZE 0x20
#define RTK_BT_GATTS_SERVICE_NUM 24
#define RTK_BLE_GAP_MAX_LINKS 3

extern write_reconnect_ptr p_roaming_start_callback;
#endif // defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C

#if defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2
#include <wifi_api.h>
#include <bt_api_config.h>
#include <rtk_stack_config.h>
#include <wifi_fast_connect.h>
#endif // defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2

#if defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616
#include <easyflash.h>
#include <wifi_mgmr_ext.h>
#include <bluetooth.h>
#include <l2cap.h>
#include <gatt.h>
#include <hci_core.h>
#include <hci_driver.h>

#define API_TASK_STACK_SIZE 256*12
#define API_TASK_PRIORITY 4
#define API_TASK_IO_MSG_QUEUE_SIZE 0x20
#define API_TASK_GAP_MSG_QUEUE_SIZE 0x20
#define RTK_BT_GATTS_SERVICE_NUM 24
#define RTK_BLE_GAP_MAX_LINKS 3

#define HI_WORD(x) ((uint8_t)((x & 0xFF00) >> 8))
#define LO_WORD(x) ((uint8_t)(x))
#endif // defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616

#if defined BL602 || defined BL702
#include <aos/kernel.h>
#include <aos/yloop.h>
#include <event_device.h>
#include <bl_sys.h>
#include <hal_wifi.h>
#include <ble_lib_api.h>
#include <uuid.h>
#endif // defined BL602 || defined BL702

#if defined BL606P || defined BL808 || defined BL616
#include <mem.h>
#include <wifi_mgmr.h>
#include <bl_fw_api.h>
#include <bflb_irq.h>
#include <bl616_glb.h>
#include <rfparam_adapter.h>
#include <board.h>
#include <bflb_mtd.h>
#include <btble_lib_api.h>
#include <bt_uuid.h>
#define WIFI_STACK_SIZE  (1536)
#define TASK_PRIORITY_FW (16)
#endif // defined BL606P || defined BL808 || defined BL616

#if defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C || defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2

#if 0
#define BT_ADDR_LE_PUBLIC       0x00
#define BT_ADDR_LE_RANDOM       0x01

#if F_BT_LE_PRIVACY_SUPPORT
#define BT_ADDR_LE_PUBLIC_ID    0x02
#define BT_ADDR_LE_RANDOM_ID    0x03
#endif // F_BT_LE_PRIVACY_SUPPORT
#endif // 0

/* Company Identifiers (see Bluetooth Assigned Numbers) */
#define BT_COMP_ID_LF           0x05f1 /* The Linux Foundation */

/* EIR/AD data type definitions */
#define BT_DATA_FLAGS                   0x01 /* AD flags */
#define BT_DATA_UUID16_SOME             0x02 /* 16-bit UUID, more available */
#define BT_DATA_UUID16_ALL              0x03 /* 16-bit UUID, all listed */
#define BT_DATA_UUID32_SOME             0x04 /* 32-bit UUID, more available */
#define BT_DATA_UUID32_ALL              0x05 /* 32-bit UUID, all listed */
#define BT_DATA_UUID128_SOME            0x06 /* 128-bit UUID, more available */
#define BT_DATA_UUID128_ALL             0x07 /* 128-bit UUID, all listed */
#define BT_DATA_NAME_SHORTENED          0x08 /* Shortened name */
#define BT_DATA_NAME_COMPLETE           0x09 /* Complete name */
#define BT_DATA_TX_POWER                0x0a /* Tx Power */
#define BT_DATA_SM_TK_VALUE             0x10 /* Security Manager TK Value */
#define BT_DATA_SM_OOB_FLAGS            0x11 /* Security Manager OOB Flags */
#define BT_DATA_SOLICIT16               0x14 /* Solicit UUIDs, 16-bit */
#define BT_DATA_SOLICIT128              0x15 /* Solicit UUIDs, 128-bit */
#define BT_DATA_SVC_DATA16              0x16 /* Service data, 16-bit UUID */
#define BT_DATA_GAP_APPEARANCE          0x19 /* GAP appearance */
#define BT_DATA_LE_BT_DEVICE_ADDRESS    0x1b /* LE Bluetooth Device Address */
#define BT_DATA_LE_ROLE                 0x1c /* LE Role */
#define BT_DATA_SOLICIT32               0x1f /* Solicit UUIDs, 32-bit */
#define BT_DATA_SVC_DATA32              0x20 /* Service data, 32-bit UUID */
#define BT_DATA_SVC_DATA128             0x21 /* Service data, 128-bit UUID */
#define BT_DATA_LE_SC_CONFIRM_VALUE     0x22 /* LE SC Confirmation Value */
#define BT_DATA_LE_SC_RANDOM_VALUE      0x23 /* LE SC Random Value */
#define BT_DATA_URI                     0x24 /* URI */
#define BT_DATA_MESH_PROV               0x29 /* Mesh Provisioning PDU */
#define BT_DATA_MESH_MESSAGE            0x2a /* Mesh Networking PDU */
#define BT_DATA_MESH_BEACON             0x2b /* Mesh Beacon */

#define BT_DATA_MANUFACTURER_DATA       0xff /* Manufacturer Specific Data */

#define BT_LE_AD_LIMITED                0x01 /* Limited Discoverable */
#define BT_LE_AD_GENERAL                0x02 /* General Discoverable */
#define BT_LE_AD_NO_BREDR               0x04 /* BR/EDR not supported */

/* Defined GAP timers */
#define BT_GAP_SCAN_FAST_INTERVAL               0x0060  /* 60 ms    */
#define BT_GAP_SCAN_FAST_WINDOW                 0x0030  /* 30 ms    */
#define BT_GAP_SCAN_SLOW_INTERVAL_1             0x0800  /* 1.28 s   */
#define BT_GAP_SCAN_SLOW_WINDOW_1               0x0012  /* 11.25 ms */
#define BT_GAP_SCAN_SLOW_INTERVAL_2             0x1000  /* 2.56 s   */
#define BT_GAP_SCAN_SLOW_WINDOW_2               0x0012  /* 11.25 ms */

#if defined(BFLB_BLE)
#define CONFIG_BT_BACKGROUND_SCAN_INTERVAL 0x0800
#define CONFIG_BT_BACKGROUND_SCAN_WINDOW 0x0012
#define BT_GAP_ADV_FAST_INT_MIN_3               0x0020  /* 20 ms   */
#define BT_GAP_ADV_FAST_INT_MAX_3               0x0020  /* 20 ms   */
#endif

#define BT_GAP_ADV_FAST_INT_MIN_1               0x0030  /* 30 ms    */
#define BT_GAP_ADV_FAST_INT_MAX_1               0x0060  /* 60 ms    */
#define BT_GAP_ADV_FAST_INT_MIN_2               0x00a0  /* 100 ms   */
#define BT_GAP_ADV_FAST_INT_MAX_2               0x00f0  /* 150 ms   */


#define BT_GAP_ADV_SLOW_INT_MIN                 0x0640  /* 1 s      */
#define BT_GAP_ADV_SLOW_INT_MAX                 0x0780  /* 1.2 s    */
#define BT_GAP_INIT_CONN_INT_MIN                0x0018  /* 30 ms    */
#define BT_GAP_INIT_CONN_INT_MAX                0x0028  /* 50 ms    */

/** Advertising options */
enum {
    /** Convenience value when no options are specified. */
    BT_LE_ADV_OPT_NONE = 0,

    /** Advertise as connectable. Type of advertising is determined by
     * providing SCAN_RSP data and/or enabling local privacy support.
     */
    BT_LE_ADV_OPT_CONNECTABLE = BIT(0),

    /** Don't try to resume connectable advertising after a connection.
     *  This option is only meaningful when used together with
     *  BT_LE_ADV_OPT_CONNECTABLE. If set the advertising will be stopped
     *  when bt_le_adv_stop() is called or when an incoming (slave)
     *  connection happens. If this option is not set the stack will
     *  take care of keeping advertising enabled even as connections
     *  occur.
     */
    /* if defined CONFIG_BLE_MULTI_ADV , Only support adv one time.*/
    BT_LE_ADV_OPT_ONE_TIME = BIT(1),

    /** Advertise using the identity address as the own address.
     *  @warning This will compromise the privacy of the device, so care
     *           must be taken when using this option.
     */
    BT_LE_ADV_OPT_USE_IDENTITY = BIT(2),

    /** Advertise using GAP device name */
    BT_LE_ADV_OPT_USE_NAME = BIT(3),

    /** Use low duty directed advertising mode, otherwise high duty mode
     *  will be used. This option is only effective when used with
     *  bt_conn_create_slave_le().
     */
    BT_LE_ADV_OPT_DIR_MODE_LOW_DUTY = BIT(4),

    /** Enable use of Resolvable Private Address (RPA) as the target address
     *  in directed advertisements when CONFIG_BT_PRIVACY is not enabled.
     *  This is required if the remote device is privacy-enabled and
     *  supports address resolution of the target address in directed
     *  advertisement.
     *  It is the responsibility of the application to check that the remote
     *  device supports address resolution of directed advertisements by
     *  reading its Central Address Resolution characteristic.
     */
    BT_LE_ADV_OPT_DIR_ADDR_RPA = BIT(5),

    /** Use whitelist to filter devices that can request scan response
     *  data.
     */
    BT_LE_ADV_OPT_FILTER_SCAN_REQ = BIT(6),

    /** Use whitelist to filter devices that can connect. */
    BT_LE_ADV_OPT_FILTER_CONN = BIT(7),
};

/** LE Advertising Parameters. */
struct bt_le_adv_param {
    /** Local identity */
    // uint8_t id;

    /** Bit-field of advertising options */
    uint8_t options;

    /** Minimum Advertising Interval (N * 0.625) */
    uint16_t interval_min;

    /** Maximum Advertising Interval (N * 0.625) */
    uint16_t interval_max;

    // uint8_t addr_type;
};

/** Helper to declare advertising parameters inline
  *
  * @param _options   Advertising Options
  * @param _int_min   Minimum advertising interval
  * @param _int_max   Maximum advertising interval
  */
#define BT_LE_ADV_PARAM(_options, _int_min, _int_max) \
        (&(struct bt_le_adv_param) { \
            .options = (_options), \
            .interval_min = (_int_min), \
            .interval_max = (_int_max), \
         })

#define BT_LE_ADV_CONN BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE, \
                       BT_GAP_ADV_FAST_INT_MIN_2, \
                       BT_GAP_ADV_FAST_INT_MAX_2)

#define BT_LE_ADV_CONN_NAME BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE | \
                        BT_LE_ADV_OPT_USE_NAME, \
                        BT_GAP_ADV_FAST_INT_MIN_2, \
                        BT_GAP_ADV_FAST_INT_MAX_2)

#define BT_LE_ADV_CONN_DIR_LOW_DUTY \
    BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_ONE_TIME | \
            BT_LE_ADV_OPT_DIR_MODE_LOW_DUTY, \
            BT_GAP_ADV_FAST_INT_MIN_2, BT_GAP_ADV_FAST_INT_MAX_2)

#define BT_LE_ADV_CONN_DIR BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE | \
                       BT_LE_ADV_OPT_ONE_TIME, 0, 0)

#define BT_LE_ADV_NCONN BT_LE_ADV_PARAM(0, BT_GAP_ADV_FAST_INT_MIN_2, \
                    BT_GAP_ADV_FAST_INT_MAX_2)

#define BT_LE_ADV_NCONN_NAME BT_LE_ADV_PARAM(BT_LE_ADV_OPT_USE_NAME, \
                         BT_GAP_ADV_FAST_INT_MIN_2, \
                         BT_GAP_ADV_FAST_INT_MAX_2)

enum {
    /* Filter duplicates. */
    BT_LE_SCAN_FILTER_DUPLICATE = BIT(0),

    /* Filter using whitelist. */
    BT_LE_SCAN_FILTER_WHITELIST = BIT(1),

    /* Filter using extended filter policies. */
    BT_LE_SCAN_FILTER_EXTENDED = BIT(2),
};

enum {
    /* Scan without requesting additional information from advertisers. */
    BT_LE_SCAN_TYPE_PASSIVE = 0x00,

    /* Scan and request additional information from advertisers. */
    BT_LE_SCAN_TYPE_ACTIVE = 0x01,
};

/** LE scan parameters */
struct bt_le_scan_param {
    /** Scan type (BT_LE_SCAN_TYPE_ACTIVE or BT_LE_SCAN_TYPE_PASSIVE) */
    uint8_t  type;

    /** Bit-field of scanning filter options. */
    uint8_t  filter_dup;

    /** Scan interval (N * 0.625 ms) */
    uint16_t interval;

    /** Scan window (N * 0.625 ms) */
    uint16_t window;
};

/** Helper to declare scan parameters inline
  *
  * @param _type     Scan Type, BT_LE_SCAN_TYPE_ACTIVE or
  *                  BT_LE_SCAN_TYPE_PASSIVE.
  * @param _filter   Filter options
  * @param _interval Scan Interval (N * 0.625 ms)
  * @param _window   Scan Window (N * 0.625 ms)
  */
#define BT_LE_SCAN_PARAM(_type, _filter, _interval, _window) \
        (&(struct bt_le_scan_param) { \
            .type = (_type), \
            .filter_dup = (_filter), \
            .interval = (_interval), \
            .window = (_window), \
         })

/** Helper macro to enable active scanning to discover new devices. */
#define BT_LE_SCAN_ACTIVE BT_LE_SCAN_PARAM(BT_LE_SCAN_TYPE_ACTIVE, \
                       BT_LE_SCAN_FILTER_DUPLICATE, \
                       BT_GAP_SCAN_FAST_INTERVAL, \
                       BT_GAP_SCAN_FAST_WINDOW)

/** Helper macro to enable passive scanning to discover new devices.
 *
 * This macro should be used if information required for device identification
 * (e.g., UUID) are known to be placed in Advertising Data.
 */
#define BT_LE_SCAN_PASSIVE BT_LE_SCAN_PARAM(BT_LE_SCAN_TYPE_PASSIVE, \
                        BT_LE_SCAN_FILTER_DUPLICATE, \
                        BT_GAP_SCAN_FAST_INTERVAL, \
                        BT_GAP_SCAN_FAST_WINDOW)

#endif // defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C || defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2

}

#define my_assert(x) do { if (!(x)) { printf("Assertion failed: %s, %s, %d\n", #x, __FILE__, __LINE__); while(1); } } while(0)

__attribute__((__weak__)) void *__dso_handle = (void *)&__dso_handle;

#if defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616
namespace std {

void __throw_bad_function_call() { my_assert("__throw_bad_function_call" == nullptr); }

} // namespace std
#endif // defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616

#include <memory>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <functional>
#include <type_traits>
#include <exception>
#include <stdexcept>
#include <array>
#include <vector>
#include <sstream>
#include <string>
#include <map>
#include <iomanip>
#include <algorithm>
#include <iterator>
#include <initializer_list>

namespace {

using ::std::make_shared;
using ::std::shared_ptr;
using ::std::unique_ptr;
using ::std::weak_ptr;
using ::std::static_pointer_cast;
using ::std::dynamic_pointer_cast;
using ::std::puts;
using ::std::printf;
using ::std::function;
using ::std::size_t;
using ::std::is_void_v;
using ::std::is_scalar_v;
using ::std::is_same_v;
using ::std::false_type;
using ::std::true_type;
using ::std::integral_constant;
using ::std::enable_shared_from_this;
using ::std::array;
using ::std::vector;
using ::std::string;
using ::std::stringstream;
using ::std::setfill;
using ::std::setw;
using ::std::hex;
using ::std::map;
using ::std::exception;
using ::std::runtime_error;
using ::std::copy_n;
using ::std::min;
using ::std::distance;
using ::std::size;
using ::std::begin;
using ::std::reverse_copy;
using ::std::fill;
using ::std::default_delete;
using ::std::initializer_list;

class RefCountObject : public enable_shared_from_this<RefCountObject> {
private:

public:
    explicit RefCountObject() {}

    RefCountObject(const RefCountObject&) = delete;

    RefCountObject(const RefCountObject&&) = delete;

    RefCountObject& operator=(const RefCountObject&) = delete;

    RefCountObject& operator=(RefCountObject&&) = delete;

    virtual ~RefCountObject() {}
};

class Semaphore : public RefCountObject {
private:
    const ::SemaphoreHandle_t m_handle = ::xSemaphoreCreateBinary();

    bool takeInternal(uint32_t timeoutTicks) const {
        return ::xSemaphoreTake(m_handle, timeoutTicks) == pdTRUE;
    }

public:
    explicit Semaphore() : RefCountObject() {
        my_assert(m_handle);
    }

    virtual ~Semaphore() {
        ::vSemaphoreDelete(m_handle);
    }

    bool give() const {
        return ::xSemaphoreGive(m_handle) == pdTRUE;
    }

    void take() const {
        my_assert(takeInternal(portMAX_DELAY));
    }

    bool take(uint32_t timeoutMsec) const {
        return takeInternal(pdMS_TO_TICKS(timeoutMsec));
    }
};

class Task : public RefCountObject {
private:
    const Semaphore m_startedSemaphore;

    const Semaphore m_exitedSemaphore;

    static void taskEntry(void *arg) {
        shared_ptr<Task> *pp = static_cast<shared_ptr<Task> *>(arg);
        (*pp)->routine();
        (*pp)->m_exitedSemaphore.give();
        delete pp;
        ::vTaskDelete(nullptr);
    }

protected:
    virtual void routine() {
        // my_assert("Should not run here" == nullptr);
    }

public:
    explicit Task() : RefCountObject() {}

    bool startSync() {
        if (m_startedSemaphore.give()) {
            routine();
            m_exitedSemaphore.give();
            return true;
        }
        else {
            printf("Sync Task is already started\n");
            return false;
        }
    }

    bool startAsync(string name, const uint32_t stackSize = 1024, const uint8_t priority = tskIDLE_PRIORITY) const {
        (void) stackSize;
        (void) priority;

        my_assert(name.empty() == false);
        my_assert(stackSize >= configMINIMAL_STACK_SIZE);
        my_assert(priority < configMAX_PRIORITIES);
        if (m_startedSemaphore.give()) {
            auto p = new shared_ptr(shared_from_this());
            my_assert(::xTaskCreate(taskEntry, name.c_str(), stackSize, p, priority, nullptr));
            return true;
        }
        else {
            printf("Async Task is already started\n");
            return false;
        }
    }

    virtual void waitForStart() const {
        m_startedSemaphore.take();
        m_startedSemaphore.give();
    }

    bool waitForStart(uint32_t timeoutMsec) const {
        if (m_startedSemaphore.take(timeoutMsec)) {
            m_startedSemaphore.give();
            return true;
        }
        return false;
    }

    void waitForExit() const {
        m_exitedSemaphore.take();
        m_exitedSemaphore.give();
    }

    bool waitForExit(uint32_t timeoutMsec) const {
        if (m_exitedSemaphore.take(timeoutMsec)) {
            m_exitedSemaphore.give();
            return true;
        }
        return false;
    }

    virtual ~Task() {
        if (!m_startedSemaphore.take(0)) {
            // Task was never started
            return;
        }
#if 0
        while (!m_exitedSemaphore.take(3000)) {
            printf("Waiting for task to exit...\n");
        }
        printf("Task exited\n");
#else
        m_exitedSemaphore.take();
#endif
    }
};

class HelloWorldTask : public Task {
protected:
    virtual void routine() override {
        for (int i = 0; i < 5; ++i) {
            printf("HelloWorldTask started: %d\n", i);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

public:
    explicit HelloWorldTask() : Task() {}

    virtual ~HelloWorldTask() override = default;
};

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

template<typename T>
class RtosQueueHolder : public RefCountObject {
private:
    const ::QueueHandle_t m_handle;

    const size_t m_length;

protected:
    bool receiveInternal(T* item, uint32_t timeoutTicks) const {
        (void) item;
        (void) timeoutTicks;
        return ::xQueueReceive(m_handle, item, timeoutTicks) == pdTRUE;
    }

    bool sendInternal(T* item, uint32_t timeoutTicks) const {
        (void) item;
        (void) timeoutTicks;
        return ::xQueueSend(m_handle, item, timeoutTicks) == pdTRUE;
    }

public:
    explicit RtosQueueHolder(size_t length) : RefCountObject(), m_handle(::xQueueCreate(length, sizeof(T))), m_length(length) {
        my_assert(m_handle);
    }

    virtual ~RtosQueueHolder() {
        ::vQueueDelete(m_handle);
    }

    ::QueueHandle_t handle() const {
        return m_handle;
    }

    size_t length() const {
        return m_length;
    }

    void receive(T *item) const {
        (void) item;
        my_assert(receiveInternal(item, portMAX_DELAY));
    }

    bool receive(T *item, uint32_t timeoutMsec) const {
        (void) item;
        (void) timeoutMsec;
        return receiveInternal(item, pdMS_TO_TICKS(timeoutMsec));
    }

    void send(T *item) const {
        (void) item;
        my_assert(sendInternal(item, portMAX_DELAY));
    }

    bool send(T *item, uint32_t timeoutMsec) const {
        (void) item;
        (void) timeoutMsec;
        return sendInternal(item, pdMS_TO_TICKS(timeoutMsec));
    }
};

template<typename T>
class SharedPtrQueue : public RtosQueueHolder<shared_ptr<T> *> {
public:
    using Parent = RtosQueueHolder<shared_ptr<T> *>;

private:
    shared_ptr<T> receiveInternal(uint32_t timeoutTicks) const {
        shared_ptr<T> *pp = nullptr;
        if (Parent::receiveInternal(&pp, timeoutTicks)) {
            my_assert(pp != nullptr);
            shared_ptr<T> p = *pp;
            delete pp;
            return p;
        }
        return nullptr;
    }

    bool sendInternal(const shared_ptr<T> &item, uint32_t timeoutTicks) const {
        shared_ptr<T> *p = new shared_ptr<T>(item);
        bool ret = Parent::sendInternal(&p, timeoutTicks);
        if (!ret) {
            delete p;
        }
        return ret;
    }

public:
    explicit SharedPtrQueue(size_t length) : RtosQueueHolder<shared_ptr<T> *>(length) {
    }

    bool send(const shared_ptr<T> &item) const {
        return sendInternal(item, portMAX_DELAY);
    }

    bool send(const shared_ptr<T> &item, uint32_t timeoutMsec) const {
        return sendInternal(item, pdMS_TO_TICKS(timeoutMsec));
    }

    shared_ptr<T> receive() const {
        return receiveInternal(portMAX_DELAY);
    }

    shared_ptr<T> receive(uint32_t timeoutMsec) const {
        return receiveInternal(pdMS_TO_TICKS(timeoutMsec));
    }

    virtual ~SharedPtrQueue() override {
        while (receive(0) != nullptr) {}
    }
};

struct SharedPtrQueueTestItem : public RefCountObject {
    int value = 0;

    explicit SharedPtrQueueTestItem(int v) : RefCountObject(), value(v) {
        printf("SharedPtrQueueTestItem with value %d created\n", value);
    }

    virtual ~SharedPtrQueueTestItem() {
        printf("SharedPtrQueueTestItem with value %d destroyed\n", value);
    };
};

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

class Logger : protected Semaphore {
    using LogQueue = SharedPtrQueue<char[]>;

public:
    enum Level {
        DEBUG,
        INFO,
        WARN,
        ERROR,
    };

    explicit Logger(size_t queueLength = 1, Level level = WARN)
        : Semaphore(), m_queue(queueLength), m_level(level) {
        give();
    }

private:
    const LogQueue m_queue;

    Level m_level;

protected:
    virtual void flush() {
        take();
        for (;;) {
            auto buffer = m_queue.receive(0);
            if (buffer != nullptr) {
                char* p = static_cast<char*>(buffer.get());
#if defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C || defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2
                puts(p);
#else
                printf("%s\r\n", p);
#endif // defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C || defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2
            }
            else {
                break;
            }
        }
        give();
    }

public:
    static const char* levelString(Level level) {
        const char *levelStrings[4] = { "DEBUG", "INFO", "WARN", "ERROR" };
        return levelStrings[level];
    }

    void setLevel(Level level) {
        m_level = level;
    }

    Level level() {
        return m_level;
    }

    void logFormat(Level level, const char *fmt, ...) {
        if (level >= m_level) {
            ::va_list ap1, ap2;
            ::va_start(ap1, fmt);
            ::va_copy(ap2, ap1);
            const int expectedSize = ::vsnprintf(nullptr, 0, fmt, ap1);
            ::va_end(ap1);
            my_assert(expectedSize > 0);
            size_t bufferSize = static_cast<size_t>(expectedSize) + 1;
            shared_ptr<char[]> buffer(new char[bufferSize], [](char *p) { delete[] p; });
            const int returnedSize = ::vsnprintf(buffer.get(), bufferSize, fmt, ap2);

            (void) expectedSize;
            (void) returnedSize;
            my_assert(expectedSize == returnedSize);

            m_queue.send(buffer);
            flush();
            va_end(ap2);
        }
    }

    virtual ~Logger() override {
        flush();
    }
};

Logger *s_logger = nullptr;

Logger *getLogger() {
    if (s_logger == nullptr) {
        s_logger = new Logger(1, Logger::DEBUG);
    }
    return s_logger;
}

void testLogger1() {
    printf("\n%s\n", "========= Logger Test 1 Started =========");
    {
        // auto logger = make_shared<Logger>();
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

#if 0
void testExceptionHandling1() {
    getLogger()->setLevel(Logger::DEBUG);
    getLogger()->logFormat(Logger::INFO, "========= Exception Handling Test 1 Started =========");
    {
        try {
            getLogger()->logFormat(Logger::INFO, "Throwing a standard exception...");
            throw runtime_error("This is a test exception");
        } catch (const exception &e) {
            getLogger()->logFormat(Logger::ERROR, "Caught exception: %s", e.what());
        }
    }
    getLogger()->logFormat(Logger::INFO, "========= Exception Handling Test 1 Finished =========");
}
#endif

template<typename Iterator>
string toHexString(Iterator begin, Iterator end) {
    stringstream ss;
    for (auto it = begin; it != end; ++it) {
        ss << (it == begin ? "" : " ") << hex << setw(2) << setfill('0') << static_cast<int>(*it);
    }
    return ss.str();
}

template<typename Name, typename Iterator>
void dumpHex(Name name, Iterator begin, Iterator end) {
    stringstream ss;
    ss << name << "[" << distance(begin, end) << "]: " << toHexString<Iterator>(begin, end);
    getLogger()->logFormat(Logger::DEBUG, "%s", ss.str().c_str());
}

struct BluetoothAdvReport {
    uint8_t addrType;
    uint8_t bdAddr[6];
    int8_t rssi;
    uint8_t advType;
    uint8_t dataLen;
    uint8_t data[1];
};

SharedPtrQueue<BluetoothAdvReport> *s_advReportQueue = nullptr;

SharedPtrQueue<BluetoothAdvReport> *getAdvReportQueue() {
    if (s_advReportQueue == nullptr) {
        s_advReportQueue = new SharedPtrQueue<BluetoothAdvReport>(10);
    }
    return s_advReportQueue;
}

class AbstractCallbackTask : public Task {
public:
    explicit AbstractCallbackTask() : Task() {}

    virtual ~AbstractCallbackTask() override = default;

    virtual bool success() const {
        return false;
    }
};

template<typename Callback, int Selected>
class _CallbackTask;

template<typename CallbackReturnType>
class _CallbackTask<CallbackReturnType, 0> : public AbstractCallbackTask {
public:
    using _Callback = function<shared_ptr<CallbackReturnType>(void)>;

private:
    const int m_selected = 0;

    const _Callback m_callback;

    shared_ptr<CallbackReturnType> m_result;

protected:
    virtual void routine() override {
        m_result = m_callback();
    }

public:
    explicit _CallbackTask(_Callback callback) : AbstractCallbackTask(), m_callback(callback) {}

    virtual ~_CallbackTask() override = default;

    shared_ptr<CallbackReturnType> result() const {
        waitForExit();
        return m_result;
    }

    virtual bool success() const override {
        waitForExit();
        return m_result != nullptr;
    }
};

template<typename CallbackVoidReturnType>
class _CallbackTask<CallbackVoidReturnType, 1> : public AbstractCallbackTask {
public:
    using _Callback = function<void(void)>;

private:
    const _Callback m_callback;

protected:
    virtual void routine() override {
        m_callback();
    }

public:
    explicit _CallbackTask(_Callback callback) : AbstractCallbackTask(), m_callback(callback) {}

    virtual ~_CallbackTask() override = default;

    void result() const {
        waitForExit();
    }

    virtual bool success() const override {
        waitForExit();
        return true;
    }
};

template<typename CallbackBoolReturnType>
class _CallbackTask<CallbackBoolReturnType, 2> : public AbstractCallbackTask {
public:
    using _Callback = function<bool(void)>;

private:
    const _Callback m_callback;

    bool m_success = false;

protected:
    virtual void routine() override {
        m_success = m_callback();
    }

public:
    explicit _CallbackTask(_Callback callback) : AbstractCallbackTask(), m_callback(callback) {}

    virtual ~_CallbackTask() override = default;

    bool result() const {
        waitForExit();
        return m_success;
    }

    virtual bool success() const override {
        return result();
    }
};

template<typename CallbackScalarReturnType>
class _CallbackTask<CallbackScalarReturnType, 3> : public AbstractCallbackTask {
public:
    using _Callback = function<CallbackScalarReturnType(void)>;

private:
    const _Callback m_callback;

    CallbackScalarReturnType m_result;

protected:
    virtual void routine() override {
        m_result = m_callback();
    }

public:
    explicit _CallbackTask(_Callback callback) : AbstractCallbackTask(), m_callback(callback) {}

    virtual ~_CallbackTask() override = default;

    CallbackScalarReturnType result() const {
        waitForExit();
        return m_result;
    }

    virtual bool success() const override {
        waitForExit();
        return true;
    }
};

template<typename CallbackSmartPointerReturnType>
class _CallbackTask<CallbackSmartPointerReturnType, 4> : public AbstractCallbackTask {
public:
    using _Callback = function<CallbackSmartPointerReturnType(void)>;

private:
    const _Callback m_callback;

    CallbackSmartPointerReturnType m_result;

protected:
    virtual void routine() override {
        m_result = m_callback();
    }

public:
    explicit _CallbackTask(_Callback callback) : AbstractCallbackTask(), m_callback(callback) {}

    virtual ~_CallbackTask() override = default;

    CallbackSmartPointerReturnType result() const {
        waitForExit();
        return m_result;
    }

    virtual bool success() const override {
        waitForExit();
        return m_result != nullptr;
    }
};

template<typename T> struct is_shared_ptr : std::false_type {};

template<typename T> struct is_shared_ptr<shared_ptr<T>> : std::true_type {};

template<typename T> constexpr bool is_shared_ptr_v = is_shared_ptr<T>::value;

template<typename T> struct is_unique_ptr : std::false_type {};

template<typename T> struct is_unique_ptr<unique_ptr<T>> : std::true_type {};

template<typename T> constexpr bool is_unique_ptr_v = is_unique_ptr<T>::value;

template<typename T> struct is_weak_ptr : std::false_type {};

template<typename T> struct is_weak_ptr<weak_ptr<T>> : std::true_type {};

template<typename T> constexpr bool is_weak_ptr_v = is_weak_ptr<T>::value;

template<typename T> struct is_smart_ptr : integral_constant<bool, is_shared_ptr_v<T> || is_unique_ptr_v<T> || is_weak_ptr_v<T>> {};

template<typename T> constexpr bool is_smart_ptr_v = is_smart_ptr<T>::value;

template<typename CallbackReturnType>
using CallbackTask = _CallbackTask<CallbackReturnType,
    is_void_v<CallbackReturnType> ? 1 :
    is_same_v<CallbackReturnType, bool> ? 2 :
    is_scalar_v<CallbackReturnType> ? 3 :
    is_smart_ptr_v<CallbackReturnType> ? 4 : 0>;

template<typename CallbackReturnType>
using Callback = typename CallbackTask<CallbackReturnType>::_Callback;

struct MacAddress {
    array<uint8_t, 6> address;

    string toString() const {
        char buf[18];
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                 address[0], address[1], address[2],
                 address[3], address[4], address[5]);
        return string(buf);
    }

    string toHex() const {
        char buf[13];
        snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X",
                 address[0], address[1], address[2],
                 address[3], address[4], address[5]);
        return string(buf);
    }
};

#if defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C || defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2

using BluetoothTaskEventQueueHolder = RtosQueueHolder<uint8_t>;

template<typename T>
class BluetoothTaskIoQueueHolder : public RtosQueueHolder<::T_IO_MSG> {
public:
    using Parent = RtosQueueHolder<::T_IO_MSG>;

    BluetoothTaskIoQueueHolder(size_t length) : RtosQueueHolder<::T_IO_MSG>(length) {}

    virtual ~BluetoothTaskIoQueueHolder() override {
        for (;;) {
            ::T_IO_MSG msg;
            if (Parent::receive(&msg, 0)) {
                if (msg.type == ::IO_MSG_TYPE_QDECODE && msg.u.buf) {
                    auto *pp = static_cast<T*>(msg.u.buf);
                    delete pp;
                }
            } else {
                break;
            }
        }
    }

protected:
    bool sendInternal(T* item, uint32_t timeoutTicks, ::T_IO_MSG_TYPE type = ::IO_MSG_TYPE_QDECODE) const {
        (void) item;
        (void) timeoutTicks;

        ::T_IO_MSG msg = {
            .type = type,
            .subtype = 0,
            .u = { .buf = static_cast<void*>(item) }
        };
        return Parent::sendInternal(&msg, timeoutTicks);
    }
};

class CallbackTaskSharedPtrIoQueue: public BluetoothTaskIoQueueHolder<shared_ptr<AbstractCallbackTask>> {
public:
    using Parent = BluetoothTaskIoQueueHolder<shared_ptr<AbstractCallbackTask>>;

protected:
    bool sendInternal(shared_ptr<AbstractCallbackTask> taskPtr, uint32_t timeoutTicks, ::T_IO_MSG_TYPE type = ::IO_MSG_TYPE_QDECODE) const {
        shared_ptr<AbstractCallbackTask> *taskPtrPtr = new shared_ptr<AbstractCallbackTask>(taskPtr);
        bool ret = Parent::sendInternal(taskPtrPtr, timeoutTicks, type);
        if (!ret) {
            delete taskPtrPtr;
        }
        return ret;
    }

public:
    explicit CallbackTaskSharedPtrIoQueue(size_t length) : BluetoothTaskIoQueueHolder<shared_ptr<AbstractCallbackTask>>(length) {}

    virtual ~CallbackTaskSharedPtrIoQueue() override {}

    bool send(shared_ptr<AbstractCallbackTask> taskPtr, ::T_IO_MSG_TYPE type = ::IO_MSG_TYPE_QDECODE) const {
        return sendInternal(taskPtr, portMAX_DELAY, type);
    }

    bool send(shared_ptr<AbstractCallbackTask> taskPtr, uint32_t timeoutMsec, ::T_IO_MSG_TYPE type = ::IO_MSG_TYPE_QDECODE) const {
        return sendInternal(taskPtr, pdMS_TO_TICKS(timeoutMsec), type);
    }
};

class LowerBluetoothTask : public Task {
public:
    bool stackRunning() const {
        ::T_GAP_DEV_STATE state;
        ::le_get_gap_param(GAP_PARAM_DEV_STATE, &state);
        return state.gap_init_state == GAP_INIT_STATE_STACK_READY;
    }

private:
    void waitForStackRunning() const {
        while (!stackRunning()) {
            ::vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    static ::T_APP_RESULT gapCallback(uint8_t type, void *args) {
        ::T_LE_CB_DATA *data = static_cast<::T_LE_CB_DATA*>(args);
        switch (type) {
#if defined(F_BT_LE_4_2_DATA_LEN_EXT_SUPPORT) && F_BT_LE_4_2_DATA_LEN_EXT_SUPPORT
            case GAP_MSG_LE_DATA_LEN_CHANGE_INFO: {
                getLogger()->logFormat(Logger::DEBUG, "GAP_MSG_LE_DATA_LEN_CHANGE_INFO");
                getLogger()->logFormat(Logger::DEBUG, "  conn_id=%d", data->p_le_data_len_change_info->conn_id);
                getLogger()->logFormat(Logger::DEBUG, "  max_tx_octets=0x%x", data->p_le_data_len_change_info->max_tx_octets);
                getLogger()->logFormat(Logger::DEBUG, "  max_tx_time=0x%x", data->p_le_data_len_change_info->max_tx_time);
                break;
            }
#endif // defined(F_BT_LE_4_2_DATA_LEN_EXT_SUPPORT) && F_BT_LE_4_2_DATA_LEN_EXT_SUPPORT
            case GAP_MSG_LE_MODIFY_WHITE_LIST: {
                getLogger()->logFormat(Logger::DEBUG, "GAP_MSG_LE_MODIFY_WHITE_LIST");
                getLogger()->logFormat(Logger::DEBUG, "  operation=%d", data->p_le_modify_white_list_rsp->operation);
                getLogger()->logFormat(Logger::DEBUG, "  cause=0x%x", data->p_le_modify_white_list_rsp->cause);
                break;
            }
            case GAP_MSG_LE_SCAN_INFO: {
                shared_ptr<BluetoothAdvReport> advReport(reinterpret_cast<BluetoothAdvReport*>(::malloc(sizeof(BluetoothAdvReport) + data->p_le_scan_info->data_len - 1)), [](BluetoothAdvReport *p) { ::free(p); });
                advReport->addrType = static_cast<uint8_t>(data->p_le_scan_info->remote_addr_type);
                ::memcpy(advReport->bdAddr, data->p_le_scan_info->bd_addr, 6);
                advReport->rssi = data->p_le_scan_info->rssi;
                advReport->advType = static_cast<uint8_t>(data->p_le_scan_info->adv_type);
                advReport->dataLen = data->p_le_scan_info->data_len;
                ::memcpy(advReport->data, data->p_le_scan_info->data, data->p_le_scan_info->data_len);
                getAdvReportQueue()->send(advReport);
                // printf("GAP_MSG_LE_SCAN_INFO bd_addr=%d scan_interval=0x%X scan_window=0x%X\n",
                //     data->scan_type,
                //     data->scan_interval,
                //     data->scan_window);
                break;
            }
        }
        return ::APP_RESULT_SUCCESS;
    }

private:
    BluetoothTaskEventQueueHolder m_eventQueue;

    CallbackTaskSharedPtrIoQueue m_ioQueue;

public:
    LowerBluetoothTask(string deviceName) : Task(), m_eventQueue(API_TASK_GAP_MSG_QUEUE_SIZE), m_ioQueue(API_TASK_IO_MSG_QUEUE_SIZE) {
        my_assert(!stackRunning());
        ::bt_trace_init();

        /* gap_config_xxx() shall be before bte_init() */
        ::gap_config_max_le_link_num(RTK_BLE_GAP_MAX_LINKS);
        ::gap_config_max_le_paired_device(RTK_BLE_GAP_MAX_LINKS);
        ::gap_config_max_mtu_size(180);
        ::gap_config_deinit_flow(0);
        ::gap_config_ccc_bits_count(16, 0);
#if F_BT_DEINIT
        ::bte_init();
#endif
        my_assert(::le_gap_init(RTK_BLE_GAP_MAX_LINKS));

        /* le_set_gap_param() shall be after bte_init() */
        uint8_t masterInitMtuRequest = 1;
        ::le_set_gap_param(GAP_PARAM_MASTER_INIT_GATT_MTU_REQ, sizeof masterInitMtuRequest, &masterInitMtuRequest);

        uint8_t slaveInitMtuRequest = 0;
        ::le_set_gap_param(GAP_PARAM_SLAVE_INIT_GATT_MTU_REQ, sizeof slaveInitMtuRequest, &slaveInitMtuRequest);
#if defined(F_BT_LE_5_0_SET_PHYS_SUPPORT) && F_BT_LE_5_0_SET_PHYS_SUPPORT
        uint8_t preferAllPhy = 0;
        ::le_set_gap_param(GAP_PARAM_DEFAULT_PHYS_PREFER, sizeof preferAllPhy, &preferAllPhy);

        uint8_t preferTxPhy = 1 | 1 << 1;
        ::le_set_gap_param(GAP_PARAM_DEFAULT_TX_PHYS_PREFER, sizeof preferTxPhy, &preferTxPhy);

        uint8_t preferRxPhy = 1 | 1 << 1;
        ::le_set_gap_param(GAP_PARAM_DEFAULT_RX_PHYS_PREFER, sizeof preferRxPhy, &preferRxPhy);
#endif

#if defined(F_BT_LE_4_2_DATA_LEN_EXT_SUPPORT) && F_BT_LE_4_2_DATA_LEN_EXT_SUPPORT
        uint16_t maxTxOctets = 0x40;
        ::le_set_gap_param(GAP_PARAM_DEFAULT_DATA_LEN_MAX_TX_OCTETS, sizeof maxTxOctets, &maxTxOctets);

        uint16_t maxTxTime = 0x200;
        ::le_set_gap_param(GAP_PARAM_DEFAULT_DATA_LEN_MAX_TX_TIME, sizeof maxTxTime, &maxTxTime);
#endif // defined(F_BT_LE_4_2_DATA_LEN_EXT_SUPPORT) && F_BT_LE_4_2_DATA_LEN_EXT_SUPPORT

        ::le_register_app_cb(gapCallback);

        static bool coexInited = false;
        if (!coexInited) {
            coexInited = true;
            ::bt_coex_init();
        }
        ::gap_config_cccd_not_check(::CONFIG_GATT_CCCD_NOT_CHECK);

        uint16_t default_appearance = GAP_GATT_APPEARANCE_UNKNOWN;
        ::gaps_set_parameter(GAPS_PARAM_APPEARANCE, sizeof(uint16_t), &default_appearance);

        uint8_t device_name_prop = GAPS_PROPERTY_WRITE_ENABLE;
        ::gaps_set_parameter(GAPS_PARAM_DEVICE_NAME_PROPERTY, sizeof(device_name_prop), &device_name_prop);

        uint8_t appearance_prop = GAPS_PROPERTY_WRITE_ENABLE;
        ::gaps_set_parameter(GAPS_PARAM_APPEARANCE_PROPERTY, sizeof(appearance_prop), &appearance_prop);

        setDeviceNameInternal(deviceName);

        ::server_builtin_service_reg(true);
        ::server_init(RTK_BT_GATTS_SERVICE_NUM - 2);
    }

    virtual ~LowerBluetoothTask() override {
        if (stackRunning()) {
#if F_BT_DEINIT
            ::bte_deinit();
#endif
#if defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C
            ::bt_trace_uninit();
#else
            ::bt_trace_deinit();
#endif
        }
    }

private:

    void onDisconnected(uint8_t connId, uint16_t reason) {
        getLogger()->logFormat(Logger::DEBUG, "onDisconnected connId=0x%X reason=0x%X", connId, reason);

        // my_assert(::le_adv_start() == GAP_CAUSE_SUCCESS);
    }

    void onConnected(uint8_t connId) {
        getLogger()->logFormat(Logger::DEBUG, "onConnected connId=0x%X", connId);
        ::le_adv_start();
    }

    bool handleGapMessage(::T_IO_MSG *ioMsg) {
        const char *gapAdvStateStrTbl[] = {
            "GAP_ADV_STATE_IDLE",
            "GAP_ADV_STATE_START",
            "GAP_ADV_STATE_ADVERTISING",
            "GAP_ADV_STATE_STOP",
        };
        const char *gapAdvSubStateStrTbl[] = {
            "GAP_ADV_TO_IDLE_CAUSE_STOP",
            "GAP_ADV_TO_IDLE_CAUSE_CONN",
        };
        const char *gapConnStateStrTbl[] = {
            "GAP_CONN_STATE_DISCONNECTED",
            "GAP_CONN_STATE_CONNECTING",
            "GAP_CONN_STATE_CONNECTED",
            "GAP_CONN_STATE_DISCONNECTING",
        };
        const char *gapInitStateStrTbl[] = {
            "GAP_INIT_STATE_INIT",
            "GAP_INIT_STATE_STACK_READY",
        };
        const char *gapScanStateStrTbl[] = {
            "GAP_SCAN_STATE_IDLE",
            "GAP_SCAN_STATE_START",
            "GAP_SCAN_STATE_SCANNING",
            "GAP_SCAN_STATE_STOP",
        };
        const char *gapAuthenStateStrTbl[] = {
            "GAP_AUTHEN_STATE_STARTED",
            "GAP_AUTHEN_STATE_COMPLETE",
        };
        const char *gap_conn_param_update_status_str_tbl[] = {
            "GAP_CONN_PARAM_UPDATE_STATUS_SUCCESS",
            "GAP_CONN_PARAM_UPDATE_STATUS_FAIL",
            "GAP_CONN_PARAM_UPDATE_STATUS_PENDING",
        };
        ::T_LE_GAP_MSG gapMsg;
        ::memcpy(&gapMsg, &ioMsg->u.param, sizeof ioMsg->u.param);

        switch (ioMsg->subtype) {
            case GAP_MSG_LE_DEV_STATE_CHANGE: {
                getLogger()->logFormat(Logger::DEBUG, "GAP_MSG_LE_DEV_STATE_CHANGE");
                getLogger()->logFormat(Logger::DEBUG, "  gap_adv_state=%s", gapAdvStateStrTbl[gapMsg.msg_data.gap_dev_state_change.new_state.gap_adv_state]);
                getLogger()->logFormat(Logger::DEBUG, "  gap_adv_sub_state=%s", gapAdvSubStateStrTbl[gapMsg.msg_data.gap_dev_state_change.new_state.gap_adv_sub_state]);
                getLogger()->logFormat(Logger::DEBUG, "  gap_conn_state=%s", gapConnStateStrTbl[gapMsg.msg_data.gap_dev_state_change.new_state.gap_conn_state]);
                getLogger()->logFormat(Logger::DEBUG, "  gap_init_state=%s", gapInitStateStrTbl[gapMsg.msg_data.gap_dev_state_change.new_state.gap_init_state]);
                getLogger()->logFormat(Logger::DEBUG, "  gap_scan_state=%s", gapScanStateStrTbl[gapMsg.msg_data.gap_dev_state_change.new_state.gap_scan_state]);
                getLogger()->logFormat(Logger::DEBUG, "  cause=0x%X", gapMsg.msg_data.gap_dev_state_change.cause);
                break;
            }
            case GAP_MSG_LE_CONN_STATE_CHANGE: {
                getLogger()->logFormat(Logger::DEBUG, "GAP_MSG_LE_CONN_STATE_CHANGE");
                getLogger()->logFormat(Logger::DEBUG, "  new_state=%s", gapConnStateStrTbl[gapMsg.msg_data.gap_conn_state_change.new_state]);
                getLogger()->logFormat(Logger::DEBUG, "  conn_id=0x%X", gapMsg.msg_data.gap_conn_state_change.conn_id);
                getLogger()->logFormat(Logger::DEBUG, "  disc_cause=0x%X", gapMsg.msg_data.gap_conn_state_change.disc_cause);
                switch (gapMsg.msg_data.gap_conn_state_change.new_state) {
                    case GAP_CONN_STATE_DISCONNECTED: {
                        onDisconnected(gapMsg.msg_data.gap_conn_state_change.conn_id,
                            gapMsg.msg_data.gap_conn_state_change.disc_cause);
                        break;
                    }
                    case GAP_CONN_STATE_CONNECTED: {
                        onConnected(gapMsg.msg_data.gap_conn_state_change.conn_id);
                        break;
                    }
                    case GAP_CONN_STATE_DISCONNECTING: {
                        break;
                    }
                    case GAP_CONN_STATE_CONNECTING: {
                        break;
                    }
                    default: {
                        break;
                    }
                }
                break;
            }
            case GAP_MSG_LE_CONN_MTU_INFO: {
                getLogger()->logFormat(Logger::DEBUG, "GAP_MSG_LE_CONN_MTU_INFO");
                getLogger()->logFormat(Logger::DEBUG, "  conn_id=0x%x", gapMsg.msg_data.gap_conn_mtu_info.conn_id);
                getLogger()->logFormat(Logger::DEBUG, "  mtu_size=%d", gapMsg.msg_data.gap_conn_mtu_info.mtu_size);
                // blufi_on_le_mtu_updated(gapMsg.msg_data.gap_conn_mtu_info.conn_id, gapMsg.msg_data.gap_conn_mtu_info.mtu_size);
                break;
            }
            case GAP_MSG_LE_CONN_PARAM_UPDATE: {
                uint8_t conn_id = gapMsg.msg_data.gap_conn_param_update.conn_id;
                getLogger()->logFormat(Logger::DEBUG, "GAP_MSG_LE_CONN_PARAM_UPDATE");
                getLogger()->logFormat(Logger::DEBUG, "  conn_id=0x%x", conn_id);
                getLogger()->logFormat(Logger::DEBUG, "  status=%s", gap_conn_param_update_status_str_tbl[gapMsg.msg_data.gap_conn_param_update.status]);
                switch (gapMsg.msg_data.gap_conn_param_update.status) {
                    case GAP_CONN_PARAM_UPDATE_STATUS_SUCCESS: {
                        uint16_t conn_interval, conn_slave_latency, conn_supervision_timeout;
                        ::le_get_conn_param(GAP_PARAM_CONN_INTERVAL, &conn_interval, conn_id);
                        ::le_get_conn_param(GAP_PARAM_CONN_LATENCY, &conn_slave_latency, conn_id);
                        ::le_get_conn_param(GAP_PARAM_CONN_TIMEOUT, &conn_supervision_timeout, conn_id);
                        // blufi_on_le_conn_param_updated(conn_id, conn_interval, conn_slave_latency, conn_supervision_timeout);
                        break;
                    }
                    case GAP_CONN_PARAM_UPDATE_STATUS_FAIL: {
                        break;
                    }
                    case GAP_CONN_PARAM_UPDATE_STATUS_PENDING: {
                    }
                    break;
                    default: {
                        break;
                    }
                }
                break;
            }
            case GAP_MSG_LE_AUTHEN_STATE_CHANGE: {
                getLogger()->logFormat(Logger::DEBUG, "GAP_MSG_LE_AUTHEN_STATE_CHANGE");
                getLogger()->logFormat(Logger::DEBUG, "  conn_id=0x%x", gapMsg.msg_data.gap_authen_state.conn_id);
                getLogger()->logFormat(Logger::DEBUG, "  new_state=%s", gapAuthenStateStrTbl[gapMsg.msg_data.gap_authen_state.new_state]);
                getLogger()->logFormat(Logger::DEBUG, "  status=0x%x", gapMsg.msg_data.gap_authen_state.status);
                break;
            }
            case GAP_MSG_LE_BOND_JUST_WORK: {
                getLogger()->logFormat(Logger::DEBUG, "GAP_MSG_LE_BOND_JUST_WORK");
                getLogger()->logFormat(Logger::DEBUG, "  conn_id=0x%x", gapMsg.msg_data.gap_bond_just_work_conf.conn_id);
                uint8_t conn_id = gapMsg.msg_data.gap_bond_just_work_conf.conn_id;
                ::le_bond_just_work_confirm(conn_id, GAP_CFM_CAUSE_ACCEPT);
                break;
            }
            case GAP_MSG_LE_BOND_PASSKEY_DISPLAY: {
                uint32_t display_value = 0;
                uint8_t conn_id = gapMsg.msg_data.gap_bond_passkey_display.conn_id;
                ::le_bond_get_display_key(conn_id, &display_value);
                ::le_bond_passkey_display_confirm(conn_id, GAP_CFM_CAUSE_ACCEPT);
                getLogger()->logFormat(Logger::DEBUG, "GAP_MSG_LE_BOND_PASSKEY_DISPLAY");
                getLogger()->logFormat(Logger::DEBUG, "  conn_id=0x%0x", conn_id);
                getLogger()->logFormat(Logger::DEBUG, "  display_value=%lu", display_value);
                break;
            }
            case GAP_MSG_LE_BOND_USER_CONFIRMATION: {
                uint32_t display_value = 0;
                uint8_t conn_id = gapMsg.msg_data.gap_bond_user_conf.conn_id;
                ::le_bond_get_display_key(conn_id, &display_value);
                // le_bond_user_confirm(conn_id, GAP_CFM_CAUSE_ACCEPT);
                getLogger()->logFormat(Logger::DEBUG, "GAP_MSG_LE_BOND_USER_CONFIRMATION");
                getLogger()->logFormat(Logger::DEBUG, "conn_id=0x%0X", conn_id);
                getLogger()->logFormat(Logger::DEBUG, "passkey=%lu", display_value);
                break;
            }
            case GAP_MSG_LE_BOND_PASSKEY_INPUT: {
                //uint32_t passkey = 888888;
                uint8_t conn_id = gapMsg.msg_data.gap_bond_passkey_input.conn_id;
                //le_bond_passkey_input_confirm(conn_id, passkey, GAP_CFM_CAUSE_ACCEPT);
                getLogger()->logFormat(Logger::DEBUG, "GAP_MSG_LE_BOND_PASSKEY_INPUT");
                getLogger()->logFormat(Logger::DEBUG, "  conn_id=0x%x", conn_id);
                break;
            }
#if F_BT_LE_SMP_OOB_SUPPORT
            case GAP_MSG_LE_BOND_OOB_INPUT: {
                uint8_t oob_data[GAP_OOB_LEN] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
                uint8_t conn_id = gapMsg.msg_data.gap_bond_oob_input.conn_id;
                ::le_bond_set_param(GAP_PARAM_BOND_OOB_DATA, GAP_OOB_LEN, oob_data);
                ::le_bond_oob_input_confirm(conn_id, GAP_CFM_CAUSE_ACCEPT);
                getLogger()->logFormat(Logger::DEBUG, "GAP_MSG_LE_BOND_OOB_INPUT");
                getLogger()->logFormat(Logger::DEBUG, "  conn_id=0x%X", conn_id);
                getLogger()->logFormat(Logger::DEBUG, "  oob_data=%s", toHexString(oob_data, oob_data + GAP_OOB_LEN).c_str());
                break;
            }
#endif
            default: {
                getLogger()->logFormat(Logger::DEBUG, "%s() unknown subtype %d", __func__, ioMsg->subtype);
                break;
            }
        }
        return true;
    }

protected:
    virtual void routine() override {
        my_assert(::gap_start_bt_stack(m_eventQueue.handle(), m_ioQueue.handle(), m_eventQueue.length() + m_ioQueue.length()));
        for (;;) {
            uint8_t event = 0;
            m_eventQueue.receive(&event);
            if (event == ::EVENT_IO_TO_APP) {
                ::T_IO_MSG ioMsg;
                if (m_ioQueue.receive(&ioMsg, 0)) {
                    if (ioMsg.type == ::IO_MSG_TYPE_BT_STATUS) {
                        if (!handleGapMessage(&ioMsg)) {
                            break;
                        }
                    }
                    else if (ioMsg.type == ::IO_MSG_TYPE_QDECODE) {
                        auto callbackPtrPtr = static_cast<shared_ptr<AbstractCallbackTask>*>(ioMsg.u.buf);
                        bool success = false;
                        if (callbackPtrPtr != nullptr) {
                            (*callbackPtrPtr)->startSync();
                            success = (*callbackPtrPtr)->success();
                        }
                        delete callbackPtrPtr;
                        if (!success) {
                            break;
                        }
                    }
                }
            } else {
                ::gap_handle_msg(event);
            }
        }
    }

    void notify() {
        uint8_t event = ::EVENT_IO_TO_APP;
        m_eventQueue.send(&event);
    }

    auto macAddressInternal() {
        auto macPtr = make_shared<MacAddress>();
        uint8_t mac[6];
        my_assert(::gap_get_param(GAP_PARAM_BD_ADDR, mac) == 0);
        reverse_copy(mac, mac + 6, macPtr->address.begin());
        return macPtr;
    }

    void setDeviceNameInternal(string deviceName) {
        if (deviceName.length() > GAP_DEVICE_NAME_LEN) {
            deviceName.resize(GAP_DEVICE_NAME_LEN);
        }
        ::gaps_set_parameter(GAPS_PARAM_DEVICE_NAME, deviceName.length(), const_cast<char*>(deviceName.c_str()));
    }

    bool stopAdvertisingInternal() {
        return ::le_adv_stop() == ::GAP_CAUSE_SUCCESS;
    }

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

    void setAdvertisingParametersInternal(const struct bt_le_adv_param *param, bool hasScanResponseData) {
        uint8_t adv_direct_type = GAP_REMOTE_ADDR_LE_PUBLIC;
#if 0
        switch (param->addr_type) {
            case BT_ADDR_LE_PUBLIC: adv_direct_type = GAP_REMOTE_ADDR_LE_PUBLIC; break;
            case BT_ADDR_LE_RANDOM: adv_direct_type = GAP_REMOTE_ADDR_LE_RANDOM; break;
#if F_BT_LE_PRIVACY_SUPPORT
            case BT_ADDR_LE_PUBLIC_ID: adv_direct_type = GAP_REMOTE_ADDR_LE_PUBLIC_IDENTITY; break;
            case BT_ADDR_LE_RANDOM_ID: adv_direct_type = GAP_REMOTE_ADDR_LE_RANDOM_IDENTITY; break;
#endif // F_BT_LE_PRIVACY_SUPPORT
            default: adv_direct_type = GAP_REMOTE_ADDR_LE_PUBLIC; break;
        }
#endif // 0
        ::le_adv_set_param(GAP_PARAM_ADV_DIRECT_ADDR_TYPE, sizeof(adv_direct_type), &adv_direct_type);

        uint8_t adv_evt_type = GAP_ADTYPE_ADV_NONCONN_IND;
        if (param->options & BT_LE_ADV_OPT_CONNECTABLE) {
            adv_evt_type = GAP_ADTYPE_ADV_IND;
        }
        else {
            if (hasScanResponseData) {
                adv_evt_type = GAP_ADTYPE_ADV_SCAN_IND;
            } else {
                adv_evt_type = GAP_ADTYPE_ADV_NONCONN_IND;
            }
        }
        ::le_adv_set_param(GAP_PARAM_ADV_EVENT_TYPE, sizeof(adv_evt_type), &adv_evt_type);

        uint8_t adv_direct_addr[GAP_BD_ADDR_LEN] = {0};
        ::le_adv_set_param(GAP_PARAM_ADV_DIRECT_ADDR, sizeof(adv_direct_addr), adv_direct_addr);

        uint8_t adv_chann_map = GAP_ADVCHAN_ALL;
        ::le_adv_set_param(GAP_PARAM_ADV_CHANNEL_MAP, sizeof(adv_chann_map), &adv_chann_map);
        
        uint8_t adv_filter_policy = GAP_ADV_FILTER_ANY;
        if (param->options & BT_LE_ADV_OPT_FILTER_SCAN_REQ) {
            adv_filter_policy = GAP_ADV_FILTER_WHITE_LIST_SCAN;
        }
        else if (param->options & GAP_ADV_FILTER_WHITE_LIST_CONN) {
            adv_filter_policy = GAP_ADV_FILTER_WHITE_LIST_CONN;
        }
        ::le_adv_set_param(GAP_PARAM_ADV_FILTER_POLICY, sizeof(adv_filter_policy), &adv_filter_policy);
        
        ::le_adv_set_param(GAP_PARAM_ADV_INTERVAL_MIN, sizeof(param->interval_min), const_cast<uint16_t*>(&param->interval_min));
        ::le_adv_set_param(GAP_PARAM_ADV_INTERVAL_MAX, sizeof(param->interval_max), const_cast<uint16_t*>(&param->interval_max));
    }

    template<typename AdvertisingFieldIterator, typename ScanResponseFieldIterator>
    bool startAdvertisingInternal(const struct bt_le_adv_param param, AdvertisingFieldIterator adBegin, size_t adLength, ScanResponseFieldIterator sdBegin, size_t sdLength) {
        setAdvertisingParametersInternal(&param, sdLength ? true : false);
        setAdvertisingDataInternal(adBegin, adLength, sdBegin, sdLength);
        return ::le_adv_start() == ::GAP_CAUSE_SUCCESS;
    }

public:
    virtual void waitForStart() const override {
        Task::waitForStart();
        waitForStackRunning();
    }

    void sendCallback(shared_ptr<AbstractCallbackTask> callback) {
        m_ioQueue.send(callback);
        notify();
    }
};
#endif // defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C || defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2

#if defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616
class LowerBluetoothTask : public Task {
private:
    SharedPtrQueue<AbstractCallbackTask> m_callbackQueue;

public:
    LowerBluetoothTask(string deviceName) : Task(), m_callbackQueue(API_TASK_IO_MSG_QUEUE_SIZE) {
#if defined BL602 || defined BL702
        ::ble_controller_init(configMAX_PRIORITIES - 1);
#endif // defined BL602 || defined BL702
#if defined BL606P || defined BL808 || defined BL616
        ::btble_controller_init(configMAX_PRIORITIES - 1);
#endif // defined BL606P || defined BL808 || defined BL616
        ::hci_driver_init();
        ::bt_enable(nullptr);

        setDeviceNameInternal(deviceName);
    }

    virtual ~LowerBluetoothTask() override {
        ::bt_disable();
    }

    void sendCallback(shared_ptr<AbstractCallbackTask> callback) {
        m_callbackQueue.send(callback);
    }

protected:
    auto macAddressInternal() {
        auto macPtr = make_shared<MacAddress>();
        ::bt_addr_le_t local_addr;
        my_assert(::bt_get_local_public_address(&local_addr) == 0);
        reverse_copy(local_addr.a.val, local_addr.a.val + 6, begin(macPtr->address));
        return macPtr;
    }

    void setDeviceNameInternal(string deviceName) {
        ::bt_set_name(deviceName.c_str());
    }

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

    bool stopAdvertisingInternal() {
        return ::bt_le_adv_stop() == 0;
    }

    void routine() override {
        for (;;) {
            auto callbackTaskPtr = m_callbackQueue.receive();
            my_assert(callbackTaskPtr != nullptr);
            callbackTaskPtr->startSync();
            if (!callbackTaskPtr->success()) {
                break;
            }
        }
    }
};
#endif // defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616

struct BluetoothTask : public LowerBluetoothTask {

    explicit BluetoothTask(string deviceName = "BT_DEVICE") : LowerBluetoothTask(deviceName) {}

    template<typename CallbackReturnType>
    auto sendCallback(Callback<CallbackReturnType> callback) {
        auto callbackTaskPtr = make_shared<CallbackTask<CallbackReturnType>>(callback);
        LowerBluetoothTask::sendCallback(static_pointer_cast<AbstractCallbackTask>(callbackTaskPtr));
        return callbackTaskPtr->result();
    }

    void exit() {
        sendCallback<bool>([]() {
            return false;
        });
        waitForExit();
    }

    shared_ptr<MacAddress> macAddress() {
        return sendCallback<MacAddress>([this]() {
            return macAddressInternal();
        });
    }

    void setDeviceName(string deviceName) {
        return sendCallback<void>([deviceName, this]() {
            setDeviceNameInternal(deviceName);
        });
    }

    template<typename BluetoothFieldList>
    bool startAdvertising(const struct bt_le_adv_param &param, const BluetoothFieldList &ad, const BluetoothFieldList &sd) {
        return sendCallback<bool>([param, ad, sd, this]() {
            return startAdvertisingInternal(param, begin(ad), size(ad), begin(sd), size(sd));
        });
    }

    template<typename BluetoothFieldList>
    bool updateAdvertisingData(const BluetoothFieldList &ad, const BluetoothFieldList &sd) {
        return sendCallback<bool>([ad, sd, this]() {
            return updateAdvertisingDataInternal(begin(ad), size(ad), begin(sd), size(sd));
        });
    }

    bool stopAdvertising() {
        return sendCallback<bool>([this]() {
            return stopAdvertisingInternal();
        });
    }
};

template<typename Uint8Container = vector<uint8_t>>
struct BluetoothField {

    uint8_t type;
    
    Uint8Container data;
};

template<typename Uint8Container = vector<uint8_t>, template<typename...> typename List = initializer_list>
using BluetoothFieldList = List<BluetoothField<Uint8Container>>;

template<typename BluetoothFieldIterator>
void printBluetoothFieldList(const char *name, BluetoothFieldIterator inputBegin, size_t inputLength) {
    getLogger()->logFormat(Logger::DEBUG, "Advertisement %s has %zu field(s):", name, inputLength);
    while (inputLength-- > 0) {
        getLogger()->logFormat(Logger::DEBUG, "  Field: type=0x%02X data_len=%zu data=%s", inputBegin->type, size(inputBegin->data), toHexString(data(inputBegin->data), data(inputBegin->data) + size(inputBegin->data)).c_str());
        ++inputBegin;
    }
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
            // .addr_type = BT_ADDR_LE_PUBLIC,
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

        // for (int i = 0; i < 2; i++)
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
        // ::vTaskDelay(pdMS_TO_TICKS(1000));
        // getLogger()->logFormat(Logger::INFO, "Starting scan...");
        // a->startScan();
        // ::vTaskDelay(pdMS_TO_TICKS(5000));
        // getLogger()->logFormat(Logger::INFO, "Stopping scan...");
        // a->stopScan();
        // ::vTaskDelay(pdMS_TO_TICKS(1000));
        getLogger()->logFormat(Logger::INFO, "Exiting Bluetooth task...");
        a->exit();
    }
    getLogger()->logFormat(Logger::INFO, "========= Bluetooth Task Test 1 Finished =========");
}

class AppTask : public Task {
protected:
    virtual void routine() override {
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
};

} // namespace

extern "C" {

int wifi_do_fast_connect(void) {
    make_shared<AppTask>()->startAsync("AppTask", 256 * 12, 4);
    return 0;
}

#if defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C
static int wlan_write_reconnect_data_to_flash(uint8_t *data, uint32_t len) {
    printf("%s() called with data length: %u\n", __func__, len);

    struct wlan_fast_reconnect *reconnect_data = (struct wlan_fast_reconnect *)data;
    if (reconnect_data == NULL || len < sizeof(struct wlan_fast_reconnect)) {
        return -1; // Invalid data or length
    }

    printf("Writing WLAN fast reconnect data to flash...\n");
    printf("PSK ESSID: %s\n", reconnect_data->psk_essid);
    printf("PSK Passphrase: %s\n", reconnect_data->psk_passphrase);
    printf("WPA Global PSK: %s\n", reconnect_data->wpa_global_PSK);
    printf("Channel: %u\n", reconnect_data->channel);
    printf("Security Type: %u\n", reconnect_data->security_type);
#if defined(CONFIG_FAST_DHCP) && CONFIG_FAST_DHCP
    printf("Offer IP: %u\n", reconnect_data->offer_ip);
#endif
#if ATCMD_VER == ATVER_2
    printf("Enable: %u\n", reconnect_data->enable);
#endif
#if defined(CONFIG_ENABLE_WPS) && CONFIG_ENABLE_WPS
    printf("Is WPS AP: %u\n", reconnect_data->is_wps_ap);
#endif
    return 0;
}
#endif // defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C

#if defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2
int write_fast_connect_data_to_flash(unsigned int offer_ip, unsigned int server_ip) {
    (void) offer_ip;
    (void) server_ip;
    return 0;
}
#endif // defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2

#if defined BL602 || defined BL702
static void event_cb_wifi_event(input_event_t *event, void *private_data) {
    (void) private_data;
    void wifi_event_handler(uint32_t code, uint32_t code2);
    wifi_event_handler(event->code, 0);
}

static void board_init(void) {
    bl_sys_init();
}
#endif // defined BL602 || defined BL702

#if defined BL606P || defined BL808 || defined BL616
static int hal_wifi_start_firmware_task(void) {
    /* enable wifi clock */
    GLB_PER_Clock_UnGate(GLB_AHB_CLOCK_IP_WIFI_PHY | GLB_AHB_CLOCK_IP_WIFI_MAC_PHY | GLB_AHB_CLOCK_IP_WIFI_PLATFORM);
    GLB_AHB_MCU_Software_Reset(GLB_AHB_MCU_SW_WIFI);

    /* Enable wifi irq */
    extern void interrupt0_handler(void);
    bflb_irq_attach(WIFI_IRQn, (irq_callback)interrupt0_handler, NULL);
    bflb_irq_enable(WIFI_IRQn);

    xTaskCreate(wifi_main, (char *)"fw", WIFI_STACK_SIZE, NULL, TASK_PRIORITY_FW, NULL);

    return 0;
}
#endif // defined BL606P || defined BL808 || defined BL616

#if defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616
void wifi_event_handler(uint32_t code, uint32_t code2) {
    (void) code2;
    switch (code) {
        case CODE_WIFI_ON_INIT_DONE: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_INIT_DONE\n", __func__);
            wifi_conf_t conf = { .country_code = "CN" };
#if defined BL602 || defined BL702
            wifi_mgmr_start_background(&conf);
#endif // defined BL602 || defined BL702
#if defined BL606P || defined BL808 || defined BL616
            wifi_mgmr_init(&conf);
#endif // defined BL606P || defined BL808 || defined BL616
            break;
        }
        case CODE_WIFI_ON_MGMR_DONE: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_MGMR_DONE\n", __func__);
            wifi_do_fast_connect();
            break;
        }
        case CODE_WIFI_ON_SCAN_DONE: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_SCAN_DONE\n", __func__);
            break;
        }
        case CODE_WIFI_ON_CONNECTED: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_CONNECTED\n", __func__);
            break;
        }
        case CODE_WIFI_ON_GOT_IP: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_GOT_IP\n", __func__);
            break;
        }
        case CODE_WIFI_ON_DISCONNECT: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_DISCONNECT\n", __func__);
            break;
        }
        case CODE_WIFI_ON_AP_STARTED: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_AP_STARTED\n", __func__);
            break;
        }
        case CODE_WIFI_ON_AP_STOPPED: {
            printf("[APP] [EVT] %s, CODE_WIFI_ON_AP_STOPPED\n", __func__);
            break;
        }
        case CODE_WIFI_ON_AP_STA_ADD: {
            printf("[APP] [EVT] [AP] [ADD] %lld\n", xTaskGetTickCount());
            break;
        }
        case CODE_WIFI_ON_AP_STA_DEL: {
            printf("[APP] [EVT] [AP] [DEL] %lld\n", xTaskGetTickCount());
            break;
        }
        default: {
            printf("[APP] [EVT] Unknown code %u\n", code);
        }
    }
}

static void on_tcpip_init_done(void *arg) {
    LWIP_UNUSED_ARG(arg);
#if defined BL602 || defined BL702
    aos_post_event(EV_WIFI, CODE_WIFI_ON_INIT_DONE, 0);
#endif // defined BL602 || defined BL702
}

int main(void) {
    board_init();
    hal_wifi_start_firmware_task();
#if defined BL602 || defined BL702
    aos_register_event_filter(EV_WIFI, event_cb_wifi_event, NULL);
#endif // defined BL602 || defined BL702
#if defined BL606P || defined BL808 || defined BL616
    if (0 != rfparam_init(0, NULL, 0)) {
        printf("rfparam_init() failed\n");
        return 0;
    }
    bflb_mtd_init();
#endif // defined BL606P || defined BL808 || defined BL616
    easyflash_init();
    tcpip_init(on_tcpip_init_done, NULL);

#if defined BL606P || defined BL808 || defined BL616
    vTaskStartScheduler();
    while (1) {}
#endif // defined BL606P || defined BL808 || defined BL616
    return 0;
}
#endif // defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616

#if defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C
void app_pre_example(void) {
    // Call back from wlan driver when rssi is lower than threshold
    // p_roaming_start_callback = wifi_roaming_start;

    // Call back from wlan driver after wlan init done
    p_wlan_init_done_callback = wifi_do_fast_connect;

    // Call back from application layer after wifi_connection success
    p_write_reconnect_ptr = wlan_write_reconnect_data_to_flash;
}
#endif // defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C

} // extern "C"
