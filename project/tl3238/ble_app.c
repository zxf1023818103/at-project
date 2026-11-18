/* SPDX-License-Identifier: Apache-2.0 */
#include "ble_store_littlefs.h"
#include "app.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "timers.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "nimble/nimble_port.h"
#include "os_mgmt/os_mgmt.h"
#include "mcumgr_compat.h"
#include "mgmt/mgmt.h"
#include "controller.h"
#include "FreeRTOS_CLI.h"
#include "storage_port.h"
#ifdef TL3238_ENABLE_COC
#include "ble_coc.h"
#define DEVICE_NAME "TL3238-CoC"
#else
#define DEVICE_NAME "TL3238-mcumgr"
#endif
#ifdef TL3238_ENABLE_CS
#include "cs_app.h"
#include "ble_ras.h"
#endif

extern int smp_bt_register(void);
extern void smp_bt_reset_rx(void);
#ifdef TL3238_ENABLE_OTA
extern void tl3238_ota_init(void);
#endif
static uint8_t own_address_type;
static TimerHandle_t reset_timer;
static volatile uint32_t synced, connects, disconnects, resets, last_error, disconnect_reason;
static volatile uint16_t connection = BLE_HS_CONN_HANDLE_NONE;
static const ble_uuid128_t smp_uuid = BLE_UUID128_INIT(
    0x84,0xaa,0x60,0x74,0x52,0x8a,0x8b,0x86,0xd3,0x4c,0xb7,0x1d,0x1d,0xdc,0x53,0x8d);
static int gap_event(struct ble_gap_event *event, void *arg);

void tl3238_ble_log(const char *format, ...)
{
    char text[180];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    console_write(text);
    console_write("\r\n");
}

/* Route libc assertions to the same visible failure path as FreeRTOS. */
void __assert_func(const char *file, int line, const char *fn, const char *expr)
{
    (void)fn;
    tl3238_ble_log("assert %s:%d: %s", file, line, expr);
    vAssertCalled(file, (unsigned long)line);
    for (;;) { }
}

static void advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
#ifdef TL3238_ENABLE_COC
    fields.uuids128 = (ble_uuid128_t *)&tl3238_coc_uuid;
#else
    fields.uuids128 = (ble_uuid128_t *)&smp_uuid;
#endif
    fields.num_uuids128 = 1;
#ifndef TL3238_ENABLE_COC
    fields.uuids128_is_complete = 1;
#endif
#ifdef TL3238_ENABLE_CS
    static const ble_uuid16_t ras_uuid = BLE_UUID16_INIT(0x185b);
    fields.uuids16 = (ble_uuid16_t *)&ras_uuid;
    fields.num_uuids16 = 1; fields.uuids16_is_complete = 1;
#endif
    int rc = ble_gap_adv_set_fields(&fields);
    configASSERT(rc == 0);
    memset(&fields, 0, sizeof fields);
#ifdef TL3238_ENABLE_COC
    /* Two 128-bit UUIDs do not fit legacy advertising together. CoC goes in
     * advertising, SMP in the scan response alongside the 10-byte name. */
    fields.uuids128 = (ble_uuid128_t *)&smp_uuid;
    fields.num_uuids128 = 1;
#endif
    fields.name = (const uint8_t *)ble_svc_gap_device_name();
    fields.name_len = strlen((const char *)fields.name);
    fields.name_is_complete = 1;
    configASSERT(ble_gap_adv_rsp_set_fields(&fields) == 0);
    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = 160; /* 100 ms */
    params.itvl_max = 240;
    rc = ble_gap_adv_start(own_address_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    last_error = rc;
    configASSERT(rc == 0);
}

#ifdef TL3238_ENABLE_CS
int tl3238_ble_connect(const ble_addr_t *peer)
{
    if (!synced) return BLE_HS_ENOTSYNCED;
    if (connection != BLE_HS_CONN_HANDLE_NONE || ble_gap_conn_active()) return BLE_HS_EBUSY;
    int rc = ble_gap_adv_stop();
    if (rc && rc != BLE_HS_EALREADY) return rc;
    struct ble_gap_conn_params p = {
        .scan_itvl = 0x40, .scan_window = 0x30,
        .itvl_min = 80, .itvl_max = 80, .supervision_timeout = 600,
    }; /* 100 ms ACL gives a 60 ms CS subevent enough scheduling room. */
    rc = ble_gap_connect(own_address_type, peer, 10000, &p, gap_event, NULL);
    if (rc) advertise();
    return rc;
}
#endif

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
#ifdef TL3238_ENABLE_COC
    tl3238_coc_gap_event(event);
#endif
#ifdef TL3238_ENABLE_CS
    ble_ras_gap_event(event);
#endif
    if (event->type == BLE_GAP_EVENT_CONNECT) {
        smp_bt_reset_rx();
        if (event->connect.status == 0) {
            connection = event->connect.conn_handle;
            connects++;
            struct ble_gap_conn_desc desc;
            if (!ble_gap_conn_find(connection,&desc))
                tl3238_ble_log("BLE connected id_type=%u ota_type=%u resolved=%u",
                    desc.peer_id_addr.type,desc.peer_ota_addr.type,
                    memcmp(desc.peer_id_addr.val,desc.peer_ota_addr.val,6)!=0);
#ifdef TL3238_ENABLE_CS
            cs_app_connect(connection);
#endif
        } else {
            last_error = event->connect.status;
            advertise();
        }
    } else if (event->type == BLE_GAP_EVENT_DISCONNECT) {
#ifdef TL3238_ENABLE_CS
        cs_app_disconnect(event->disconnect.conn.conn_handle);
#endif
        connection = BLE_HS_CONN_HANDLE_NONE;
        disconnects++;
        disconnect_reason = event->disconnect.reason;
        smp_bt_reset_rx();
        advertise();
    } else if (event->type == BLE_GAP_EVENT_ENC_CHANGE) {
        struct ble_gap_conn_desc desc;
        int found = ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0;
        tl3238_ble_log("BLE security status=%d encrypted=%u bonded=%u key_size=%u",
            event->enc_change.status, found ? desc.sec_state.encrypted : 0,
            found ? desc.sec_state.bonded : 0, found ? desc.sec_state.key_size : 0);
        /* NimBLE 1.10 does not propagate the return value from its SMP key
         * writes. Do not keep a link which only appears durably bonded. */
        if (!tl3238_storage_ready()) {
            tl3238_ble_log("BLE storage failed=%d; disconnecting",tl3238_storage_error());
            (void)ble_gap_terminate(event->enc_change.conn_handle,BLE_ERR_REM_USER_CONN_TERM);
        }
    } else if (event->type == BLE_GAP_EVENT_REPEAT_PAIRING) {
        struct ble_gap_conn_desc desc;
        const struct ble_gap_repeat_pairing *r=&event->repeat_pairing;
        /* A peer which forgot its bond may pair again without downgrading
         * the existing security. Normal reconnects restore encryption. */
        if (r->new_sc && r->new_bonding && r->new_key_size>=r->cur_key_size &&
            !r->cur_authenticated && !ble_gap_conn_find(r->conn_handle,&desc) &&
            !ble_store_util_delete_peer(&desc.peer_id_addr)) return BLE_GAP_REPEAT_PAIRING_RETRY;
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    } else if (event->type == BLE_GAP_EVENT_ADV_COMPLETE) {
        advertise();
    }
    return 0;
}

static void host_sync(void)
{
    if (ble_store_littlefs_on_sync()) return;
    if (!tl3238_storage_ready()) {
        tl3238_ble_log("BLE stopped: persistent storage error=%d",tl3238_storage_error());
        return;
    }
    configASSERT(ble_hs_util_ensure_addr(0) == 0);
    configASSERT(ble_hs_id_infer_auto(0, &own_address_type) == 0);
    synced = 1;
#ifdef TL3238_ENABLE_CS
    cs_app_sync();
#endif
    advertise();
    tl3238_ble_log("NimBLE ready: " DEVICE_NAME ", SMP service, MTU 247");
}

static void host_reset(int reason)
{
#ifdef TL3238_ENABLE_COC
    tl3238_coc_reset();
#endif
#ifdef TL3238_ENABLE_CS
    cs_app_disconnect(connection);
    ble_ras_reset();
#endif
    connection = BLE_HS_CONN_HANDLE_NONE;
    synced = 0;
    resets++;
    last_error = reason;
}

static void reset_callback(TimerHandle_t timer)
{
    (void)timer;
    sys_reboot();
}

int os_mgmt_impl_reset(unsigned int delay_ms)
{
    TickType_t delay = pdMS_TO_TICKS(delay_ms < 250 ? 250 : delay_ms);
    return xTimerChangePeriod(reset_timer, delay, 0) == pdPASS ? 0 : MGMT_ERR_EUNKNOWN;
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_init();
    int storage_rc=tl3238_storage_init();
    tl3238_ble_log("LittleFS init=%d; persistent bonding enabled",storage_rc);
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
#ifdef TL3238_ENABLE_CS
    cs_app_init();
    configASSERT(ble_ras_init() == 0);
#endif
    ble_hs_cfg.sync_cb = host_sync;
    ble_hs_cfg.reset_cb = host_reset;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    configASSERT(ble_svc_gap_device_name_set(DEVICE_NAME) == 0);
    os_mgmt_register_group();
#ifdef TL3238_ENABLE_OTA
    tl3238_ota_init();
#endif
    configASSERT(smp_bt_register() == 0);
#ifdef TL3238_ENABLE_COC
    configASSERT(tl3238_coc_init() == 0);
#endif
    nimble_port_run();
}

static BaseType_t ble_command(char *out, size_t len, const char *command)
{
    BaseType_t option_length, extra_length;
    const char *option = FreeRTOS_CLIGetParameter(command, 1, &option_length);
    if (option) {
        if (option_length != 10 || strncmp(option, "disconnect", 10) ||
            FreeRTOS_CLIGetParameter(command, 2, &extra_length)) {
            snprintf(out, len, "Usage: ble [disconnect]\r\n");
            return pdFALSE;
        }
        int rc = connection == BLE_HS_CONN_HANDLE_NONE ? 0 :
            ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
        snprintf(out, len, "BLE disconnect rc=%d\r\n", rc);
        return pdFALSE;
    }
    snprintf(out, len,
        "NimBLE=1.10.0 mcumgr=0.7.0 name=" DEVICE_NAME "\r\n"
        "sync=%lu conn=%u connects=%lu disconnects=%lu resets=%lu error=%lu reason=%lu\r\n"
        "controller=%lu init=%lu irq=%lu loops=%lu cmd=%lu tx_acl=%lu evt=%lu rx_acl=%lu errors=%lu\r\n"
#ifdef TL3238_ENABLE_OTA
        "SMP: OS echo/reset/params/info; image upload/state/erase; fs unavailable\r\n",
#else
        "SMP: OS echo/reset/params/info; image/fs unavailable\r\n",
#endif
        (unsigned long)synced, (unsigned)connection, (unsigned long)connects,
        (unsigned long)disconnects, (unsigned long)resets, (unsigned long)last_error, (unsigned long)disconnect_reason,
        (unsigned long)tl3238_controller_stats.ready, (unsigned long)tl3238_controller_stats.init_error,
        (unsigned long)tl3238_controller_stats.irqs, (unsigned long)tl3238_controller_stats.loops,
        (unsigned long)tl3238_controller_stats.commands, (unsigned long)tl3238_controller_stats.tx_acl,
        (unsigned long)tl3238_controller_stats.events, (unsigned long)tl3238_controller_stats.rx_acl,
        (unsigned long)tl3238_controller_stats.errors);
    return pdFALSE;
}

void tl3238_ble_start(void)
{
    static const CLI_Command_Definition_t cmd = {"ble", "ble [disconnect]: NimBLE/controller/mcumgr status\r\n", ble_command, -1};
    configASSERT(FreeRTOS_CLIRegisterCommand(&cmd) == pdPASS);
    reset_timer = xTimerCreate("smp_reset", pdMS_TO_TICKS(250), pdFALSE, NULL, reset_callback);
    configASSERT(reset_timer);
    configASSERT(xTaskCreate(host_task, "nimble", 1536, NULL, 2, NULL) == pdPASS);
}
