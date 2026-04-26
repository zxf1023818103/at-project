#pragma once

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
    BT_LE_ADV_OPT_NONE = 0,
    BT_LE_ADV_OPT_CONNECTABLE = BIT(0),
    BT_LE_ADV_OPT_ONE_TIME = BIT(1),
    BT_LE_ADV_OPT_USE_IDENTITY = BIT(2),
    BT_LE_ADV_OPT_USE_NAME = BIT(3),
    BT_LE_ADV_OPT_DIR_MODE_LOW_DUTY = BIT(4),
    BT_LE_ADV_OPT_DIR_ADDR_RPA = BIT(5),
    BT_LE_ADV_OPT_FILTER_SCAN_REQ = BIT(6),
    BT_LE_ADV_OPT_FILTER_CONN = BIT(7),
};

/** LE Advertising Parameters. */
struct bt_le_adv_param {
    uint8_t options;
    uint16_t interval_min;
    uint16_t interval_max;
};

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
    BT_LE_SCAN_FILTER_DUPLICATE = BIT(0),
    BT_LE_SCAN_FILTER_WHITELIST = BIT(1),
    BT_LE_SCAN_FILTER_EXTENDED = BIT(2),
};

enum {
    BT_LE_SCAN_TYPE_PASSIVE = 0x00,
    BT_LE_SCAN_TYPE_ACTIVE = 0x01,
};

/** LE scan parameters */
struct bt_le_scan_param {
    uint8_t  type;
    uint8_t  filter_dup;
    uint16_t interval;
    uint16_t window;
};

#define BT_LE_SCAN_PARAM(_type, _filter, _interval, _window) \
        (&(struct bt_le_scan_param) { \
            .type = (_type), \
            .filter_dup = (_filter), \
            .interval = (_interval), \
            .window = (_window), \
         })

#define BT_LE_SCAN_ACTIVE BT_LE_SCAN_PARAM(BT_LE_SCAN_TYPE_ACTIVE, \
                       BT_LE_SCAN_FILTER_DUPLICATE, \
                       BT_GAP_SCAN_FAST_INTERVAL, \
                       BT_GAP_SCAN_FAST_WINDOW)

#define BT_LE_SCAN_PASSIVE BT_LE_SCAN_PARAM(BT_LE_SCAN_TYPE_PASSIVE, \
                        BT_LE_SCAN_FILTER_DUPLICATE, \
                        BT_GAP_SCAN_FAST_INTERVAL, \
                        BT_GAP_SCAN_FAST_WINDOW)

#endif // defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C || defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2

}  // extern "C"

#define my_assert(x) do { if (!(x)) { ::std::printf("Assertion failed: %s, %s, %d\n", #x, __FILE__, __LINE__); while(1); } } while(0)

// platform_stdlib_rtl8721d.h defines rand→Rand which breaks std::rand() in C++ headers
#undef rand
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

namespace at {

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

template<typename Iterator>
inline string toHexString(Iterator begin, Iterator end) {
    stringstream ss;
    for (auto it = begin; it != end; ++it) {
        ss << (it == begin ? "" : " ") << hex << setw(2) << setfill('0') << static_cast<int>(*it);
    }
    return ss.str();
}

template<typename T> struct is_shared_ptr : ::std::false_type {};
template<typename T> struct is_shared_ptr<shared_ptr<T>> : ::std::true_type {};
template<typename T> constexpr bool is_shared_ptr_v = is_shared_ptr<T>::value;

template<typename T> struct is_unique_ptr : ::std::false_type {};
template<typename T> struct is_unique_ptr<unique_ptr<T>> : ::std::true_type {};
template<typename T> constexpr bool is_unique_ptr_v = is_unique_ptr<T>::value;

template<typename T> struct is_weak_ptr : ::std::false_type {};
template<typename T> struct is_weak_ptr<weak_ptr<T>> : ::std::true_type {};
template<typename T> constexpr bool is_weak_ptr_v = is_weak_ptr<T>::value;

template<typename T> struct is_smart_ptr : integral_constant<bool, is_shared_ptr_v<T> || is_unique_ptr_v<T> || is_weak_ptr_v<T>> {};
template<typename T> constexpr bool is_smart_ptr_v = is_smart_ptr<T>::value;

}  // namespace at
