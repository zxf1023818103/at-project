/* BLE is the standby trigger; the shared NAN task owns radio/PASN/TCP startup.
 * blenan sleep parks the radio and re-arms BLE without rebuilding the host. */
#include "ameba_soc.h"
#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"
#include "task.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "hci_platform.h"
#include <stdio.h>
#include <string.h>

#define WAKE_NAME "Green2 NAN Wake"
extern volatile uint32_t green2_sleep_count;
extern void amebagreen2_console_sleep(int enter);
extern int app_ota_selftest_ready(void);
extern void nimble_platform_coex_set_conn(int connected);
extern int ble_hs_hci_util_read_adv_tx_pwr(int8_t *power);

enum wake_state { BOOT, STANDBY, STARTING, ACTIVE, PARKED, RESUMING, ERROR };
static volatile enum wake_state state;
static struct ble_npl_event adv_event;
static int adv_event_ready;
static TaskHandle_t nan_owner;
static volatile int advertising_ready, requested;
static uint8_t address_type;
static uint16_t connection_handle = BLE_HS_CONN_HANDLE_NONE;
static uint32_t bt_wakes, wake0, wake1;
static TickType_t connected_at;
static unsigned connections;
static int adv_rc, nan_rc;
static int8_t tx_power;

static uint32_t bt_suspend(uint32_t unused, void *arg)
{
    (void)unused; (void)arg;
    hci_platform_force_uart_rts(true);
    return 1;
}

static uint32_t bt_resume(uint32_t unused, void *arg)
{
    (void)unused; (void)arg;
    uint32_t status1 = HAL_READ32(PMC_BASE, WAK_STATUS1);
    if (status1 & (WAKE_SRC_BT_WAKE_HOST & 0x3fffffff)) {
        ++bt_wakes;
        wake0 = HAL_READ32(PMC_BASE, WAK_STATUS0);
        wake1 = status1;
        /* Give UART RX / HCI time to consume the controller's wake packet. */
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
        /* Latch the trigger before notifying the NAN owner. This lock remains
         * held even if the peer disconnects while NAN is still starting. */
        pmu_acquire_wakelock(PMU_OS);
        connection_handle = event->connect.conn_handle;
        ++connections;
        if (!requested) connected_at = xTaskGetTickCount();
        requested = 1;
        nimble_platform_coex_set_conn(1);
        printf("[BLENAN] connected handle=%u entries=%lu bt_wakes=%lu "
               "wake0=%08lx wake1=%08lx\n", connection_handle,
               (unsigned long)green2_sleep_count, (unsigned long)bt_wakes,
               (unsigned long)wake0, (unsigned long)wake1);
        TaskHandle_t owner = nan_owner;
        if (owner) xTaskNotifyGive(owner);
    } else if (event->type == BLE_GAP_EVENT_DISCONNECT) {
        connection_handle = BLE_HS_CONN_HANDLE_NONE;
        nimble_platform_coex_set_conn(0);
        printf("[BLENAN] disconnected reason=%d; NAN remains enabled\n",
               event->disconnect.reason);
    } else if ((event->type == BLE_GAP_EVENT_CONNECT && event->connect.status) ||
               event->type == BLE_GAP_EVENT_ADV_COMPLETE) {
        if (!requested) advertise();
    }
    return 0;
}

static void advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    struct ble_gap_adv_params params = {0};
    /* Existing readable/writable GATT service, advertised in the scan reply. */
    ble_uuid128_t service = BLE_UUID128_INIT(0x2d,0x71,0xa2,0x59,0xb4,0x58,0xc8,0x12,
                                           0x99,0x99,0x43,0x95,0x12,0x2f,0x46,0x59);
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (const uint8_t *)WAKE_NAME;
    fields.name_len = sizeof(WAKE_NAME) - 1;
    fields.name_is_complete = 1;
    adv_rc = ble_gap_adv_set_fields(&fields);
    memset(&fields, 0, sizeof(fields));
    fields.uuids128 = &service;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    if (!adv_rc) adv_rc = ble_gap_adv_rsp_set_fields(&fields);
    if (!adv_rc) adv_rc = ble_hs_hci_util_read_adv_tx_pwr(&tx_power);
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = params.itvl_max = 200; /* 125 ms */
    params.channel_map = 7;
    if (!adv_rc) adv_rc = ble_gap_adv_start(address_type, NULL, BLE_HS_FOREVER,
                                           &params, gap_event, NULL);
    advertising_ready = !adv_rc;
    printf("[BLENAN] advertising name=%s interval_ms=125 tx_dbm=%d rc=%d\n",
           WAKE_NAME, tx_power, adv_rc);
    TaskHandle_t owner = nan_owner;
    if (owner) xTaskNotifyGive(owner);
}

static void advertise_event(struct ble_npl_event *event)
{
    (void)event;
    if (!requested) advertise();
}

const char *nimble_platform_device_name(void) { return WAKE_NAME; }
int nimble_platform_advertise(uint8_t addr_type)
{
    address_type = addr_type;
    if (!adv_event_ready) {
        ble_npl_event_init(&adv_event, advertise_event, NULL);
        adv_event_ready = 1;
    }
    pmu_register_sleep_callback(PMU_BT_CONTROLLER, bt_suspend, NULL, bt_resume, NULL);
    if (!requested) advertise();
    return 1;
}

/* Always called on the NAN owner task, before first init or after radio stop. */
static void wait_for_connection(int retained)
{
    nan_owner = xTaskGetCurrentTaskHandle();
    while (!advertising_ready && !requested) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    while (!app_ota_selftest_ready()) vTaskDelay(pdMS_TO_TICKS(20));
    printf("[BLENAN] standby PG; BLE connection %s NAN\n", retained ? "resumes" : "starts");
    vTaskDelay(pdMS_TO_TICKS(100)); /* drain boot / advertising logs */
    taskENTER_CRITICAL();
    /* A connection can arrive during boot. Never release its newly held lock. */
    if (!requested) {
        state = retained ? PARKED : STANDBY;
        amebagreen2_console_sleep(1);
        pmu_set_sleep_type(SLEEP_PG);
        pmu_release_wakelock(PMU_OS);
    }
    taskEXIT_CRITICAL();
    while (!requested) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    pmu_acquire_wakelock(PMU_OS);
    pmu_set_sleep_type(SLEEP_CG);
    amebagreen2_console_sleep(0);
    state = retained ? RESUMING : STARTING;
    printf("[BLENAN] %s NAN after BLE connection\n", retained ? "resuming retained" : "starting");
    nan_owner = NULL;
}

void green2_nan_wait_for_start(void) { wait_for_connection(0); }

int green2_nan_power_can_pause(void)
{
    return state == ACTIVE && connection_handle == BLE_HS_CONN_HANDLE_NONE;
}

void green2_nan_power_wait(void)
{
    configASSERT(adv_event_ready);
    /* NAN's owner already stopped radio activity and cleared old sessions. */
    taskENTER_CRITICAL();
    state = PARKED;
    requested = advertising_ready = 0;
    nan_owner = xTaskGetCurrentTaskHandle();
    taskEXIT_CRITICAL();
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &adv_event);
    wait_for_connection(1);
}

void green2_nan_start_result(int rc)
{
    nan_rc = rc;
    state = rc ? ERROR : ACTIVE;
    printf("[BLENAN] NAN %s rc=%d elapsed_ms=%lu locks=%08lx\n",
           rc ? "failed" : "active", rc,
           (unsigned long)((xTaskGetTickCount() - connected_at) * portTICK_PERIOD_MS),
           (unsigned long)pmu_get_wakelock_status());
}

void green2_nan_power_resume_result(int rc)
{
    nan_rc = rc;
    state = rc ? ERROR : ACTIVE;
    printf("[BLENAN] NAN resumed rc=%d elapsed_ms=%lu locks=%08lx\n", rc,
           (unsigned long)((xTaskGetTickCount() - connected_at) * portTICK_PERIOD_MS),
           (unsigned long)pmu_get_wakelock_status());
}

static BaseType_t command(char *out, size_t length, const char *input)
{
    static const char *names[] = {"boot", "standby", "starting", "active", "parked", "resuming", "error"};
    if (!strcmp(input, "blenan sleep")) {
        extern int green2_nan_request_standby(void);
        int rc = green2_nan_request_standby();
        snprintf(out, length, "blenan: %s\r\n", rc ?
                 "sleep rejected; NAN must be active and BLE disconnected" :
                 "sleep queued; current NAN link ends, BLE connection resumes");
        return pdFALSE;
    }
    if (strcmp(input, "blenan status")) {
        snprintf(out, length, "blenan status | sleep: BLE standby / retained NAN resume\r\n");
        return pdFALSE;
    }
    snprintf(out, length, "blenan: state=%s adv=%d connected=%u connections=%u "
             "nan_rc=%d entries=%lu bt_wakes=%lu locks=%08lx\r\n",
             names[state], ble_gap_adv_active(), connection_handle != BLE_HS_CONN_HANDLE_NONE,
             connections, nan_rc, (unsigned long)green2_sleep_count,
             (unsigned long)bt_wakes, (unsigned long)pmu_get_wakelock_status());
    return pdFALSE;
}

void green2_ble_nan_register(void)
{
    static const CLI_Command_Definition_t def = {
        "blenan", "blenan status|sleep: BLE PG standby, connection starts/resumes NAN\r\n", command, -1
    };
    FreeRTOS_CLIRegisterCommand(&def);
}
