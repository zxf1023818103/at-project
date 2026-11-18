/* Opt-in advertising-only profile for comparison with TR1020 section 1.6.
 * No scan, connection procedure, Wi-Fi association, or NAN service is started.
 * WLAN initializes once for BT RF calibration and then uses IPS WIFI_OFF. */
#include "ameba_soc.h"
#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"
#include "task.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "hci_platform.h"
#include <stdio.h>
#include <string.h>

extern int ble_hs_hci_util_read_adv_tx_pwr(int8_t *power);
extern volatile uint32_t green2_sleep_count, green2_sleep_ms;
extern void amebagreen2_console_sleep(int enter);
static struct ble_npl_event adv_event;
static volatile int ready, pending;
static unsigned interval_ms = 125, connectable, connections;
static uint8_t address_type;
static int8_t tx_power = 127;
static int adv_result;
static volatile int wake_test, connection_lock;
static uint16_t connection_handle = BLE_HS_CONN_HANDLE_NONE;
static uint32_t bt_wakes, last_wake0, last_wake1;
static TickType_t connected_tick;

/* Match the controller's host-sleep handshake: defer its HCI UART traffic
 * while AP clocks are gated; BT_WAKE_HOST is routed to the AP PMC. */
static uint32_t bt_suspend(uint32_t unused, void *arg)
{
    (void)unused; (void)arg;
    hci_platform_force_uart_rts(true);
    return 1;
}
static uint32_t bt_resume(uint32_t unused, void *arg)
{
    (void)unused; (void)arg;
    last_wake0 = HAL_READ32(PMC_BASE, WAK_STATUS0);
    last_wake1 = HAL_READ32(PMC_BASE, WAK_STATUS1);
    if (last_wake1 & (WAKE_SRC_BT_WAKE_HOST & 0x3fffffff)) {
        ++bt_wakes;
        /* Let the UART RX interrupt and HCI task drain the wake packet before
         * another idle entry can assert RTS and gate the AP again. */
        pmu_set_sysactive_time(2);
    }
    hci_platform_force_uart_rts(false);
    return 1;
}

static void advertise(void);
static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type == BLE_GAP_EVENT_CONNECT && !event->connect.status) {
        ++connections;
        connection_handle = event->connect.conn_handle;
        if (wake_test) {
            if (!(pmu_get_wakelock_status() & BIT(PMU_BT_HOST))) {
                pmu_acquire_wakelock(PMU_BT_HOST);
                connection_lock = 1;
            }
            connected_tick = xTaskGetTickCount();
            printf("[BLEWAKE] connected handle=%u entries=%lu bt_wakes=%lu "
                   "wake0=%08lx wake1=%08lx locks=%08lx\n", connection_handle,
                   (unsigned long)green2_sleep_count, (unsigned long)bt_wakes,
                   (unsigned long)last_wake0, (unsigned long)last_wake1,
                   (unsigned long)pmu_get_wakelock_status());
        } else {
            printf("[BLEPOWER] unexpected connection: measurement invalid\n");
            ble_gap_terminate(connection_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
    } else if (event->type == BLE_GAP_EVENT_DISCONNECT ||
               (event->type == BLE_GAP_EVENT_CONNECT && event->connect.status)) {
        if (event->type == BLE_GAP_EVENT_DISCONNECT) {
            printf("[BLEWAKE] disconnected reason=%d entries=%lu held_ms=%lu\n",
                   event->disconnect.reason, (unsigned long)green2_sleep_count,
                   (unsigned long)(xTaskGetTickCount() - connected_tick));
            connection_handle = BLE_HS_CONN_HANDLE_NONE;
        }
        advertise();
        /* Advertising setup waits for HCI command completions. Keep the
         * connection's lock through those transactions, then return to PG. */
        if (connection_lock) {
            pmu_release_wakelock(PMU_BT_HOST);
            connection_lock = 0;
        }
    }
    return 0;
}

static void report(void)
{
    printf("[BLEPOWER] ready=%d adv=%d interval_ms=%u connectable=%u "
           "payload=22 channels=7 tx_dbm=%d rc=%d connections=%u "
           "sleep_type=%lu locks=%08lx entries=%lu sleep_ms=%lu "
           "connected=%u bt_wakes=%lu\n",
           ready, ble_gap_adv_active(), interval_ms, connectable, tx_power,
           adv_result, connections, (unsigned long)pmu_get_sleep_type(),
           (unsigned long)pmu_get_wakelock_status(),
           (unsigned long)green2_sleep_count, (unsigned long)green2_sleep_ms,
           connection_handle != BLE_HS_CONN_HANDLE_NONE, (unsigned long)bt_wakes);
}

static void advertise(void)
{
    uint8_t payload[] = {2, 1, 4, 18, 9,
        'G','r','e','e','n','2',' ','B','L','E',' ','T','e','s','t',' ','A'};
    struct ble_gap_adv_params params = {0};
    (void)ble_gap_adv_stop();
    if (!interval_ms) { adv_result = 0; report(); return; }
    payload[2] = connectable ? 6 : 4;
    params.conn_mode = connectable ? BLE_GAP_CONN_MODE_UND : BLE_GAP_CONN_MODE_NON;
    /* NON/NON selects ADV_NONCONN_IND, not scannable ADV_SCAN_IND. */
    params.disc_mode = connectable ? BLE_GAP_DISC_MODE_GEN : BLE_GAP_DISC_MODE_NON;
    params.itvl_min = params.itvl_max = interval_ms * 8 / 5;
    params.channel_map = 7;
    adv_result = ble_gap_adv_set_data(payload, sizeof(payload));
    if (!adv_result) adv_result = ble_gap_adv_rsp_set_data(NULL, 0);
    if (!adv_result) adv_result = ble_hs_hci_util_read_adv_tx_pwr(&tx_power);
    if (!adv_result) adv_result = ble_gap_adv_start(address_type, NULL,
        BLE_HS_FOREVER, &params, gap_event, NULL);
    report();
}

static void reconfigure(struct ble_npl_event *event)
{
    (void)event;
    advertise();
    pending = 0;
}

int nimble_platform_advertise(uint8_t addr_type)
{
    address_type = addr_type;
    if (!ready) {
        ble_npl_event_init(&adv_event, reconfigure, NULL);
        pmu_register_sleep_callback(PMU_BT_CONTROLLER, bt_suspend, NULL, bt_resume, NULL);
    }
    ready = 1;
    advertise();
    return 1;
}

static BaseType_t command(char *out, size_t length, const char *input)
{
    (void)out; (void)length;
    char action[16], mode[16], extra;
    unsigned value;
    int n = sscanf(input, "%*s %15s %15s %u %c", action, mode, &value, &extra);
    if (n == 1 && !strcmp(action, "status")) { report(); return pdFALSE; }
    if (!ready || pending) {
        printf("[BLEPOWER] not ready or command pending\n");
        return pdFALSE;
    }
    if (n == 3 && !strcmp(action, "adv") &&
        (!strcmp(mode, "non") || !strcmp(mode, "conn")) &&
        (value == 125 || value == 1000)) {
        interval_ms = value;
        connectable = !strcmp(mode, "conn");
    } else if (n == 1 && !strcmp(action, "stop")) {
        interval_ms = 0;
    } else if (n == 3 && (!strcmp(action, "sleep") || !strcmp(action, "wake")) &&
               (!strcmp(mode, "cg") || !strcmp(mode, "pg")) && value && value <= 300) {
        if (connection_handle != BLE_HS_CONN_HANDLE_NONE ||
            (!strcmp(action, "wake") && (!connectable || !ble_gap_adv_active()))) {
            printf("[BLEWAKE] requires unconnected connectable advertising\n");
            return pdFALSE;
        }
        unsigned count = green2_sleep_count, ms = green2_sleep_ms;
        unsigned connects = connections, wakes = bt_wakes;
        uint32_t previous_type = pmu_get_sleep_type();
        uint32_t previous_locks = pmu_get_wakelock_status();
        wake_test = !strcmp(action, "wake");
        printf("[BLEPOWER] sleep begin mode=%s seconds=%u accept_connections=%d\n", mode, value, wake_test);
        vTaskDelay(pdMS_TO_TICKS(50)); /* drain LOGUART before clock/power gating */
        amebagreen2_console_sleep(1);
        pmu_set_sleep_type(!strcmp(mode, "pg") ? SLEEP_PG : SLEEP_CG);
        pmu_release_wakelock(PMU_OS);
        vTaskDelay(pdMS_TO_TICKS(value * 1000));
        if (previous_locks & BIT(PMU_OS)) pmu_acquire_wakelock(PMU_OS);
        wake_test = 0;
        if (connection_lock) {
            pmu_release_wakelock(PMU_BT_HOST);
            connection_lock = 0;
        }
        pmu_set_sleep_type(previous_type);
        amebagreen2_console_sleep(0);
        printf("[BLEPOWER] sleep end mode=%s entries=%u elapsed_ms=%u "
               "connections=%u bt_wakes=%u\n", mode,
               (unsigned)green2_sleep_count - count, (unsigned)green2_sleep_ms - ms,
               connections - connects, (unsigned)bt_wakes - wakes);
        report();
        return pdFALSE;
    } else {
        printf("blepower status | adv non|conn 125|1000 | stop | sleep|wake cg|pg 1..300\n");
        return pdFALSE;
    }
    pending = 1;
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &adv_event);
    return pdFALSE;
}

void green2_ble_power_register(void)
{
    static const CLI_Command_Definition_t definition = {
        "blepower", "blepower: fixed BLE advertising / timed system CG or PG\r\n", command, -1
    };
    FreeRTOS_CLIRegisterCommand(&definition);
}
