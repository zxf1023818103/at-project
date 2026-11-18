/* Experimental BLE-assisted NAN PIN bootstrap. The iOS public API still
 * requires the person to enter this PIN in the system Wi-Fi Aware picker. */
#include "nan_oob.h"
#include "nan_network.h"
#include "FreeRTOS.h"
#include "task.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "ble_store_littlefs.h"
#include "mbedtls/platform_util.h"
#include <stdio.h>
#include <string.h>

/* 7D2F8A10-6F4B-4C9E-A835-91D04B72E601; characteristic ...8A11... */
static const ble_uuid128_t service = BLE_UUID128_INIT(
    0x01,0xe6,0x72,0x4b,0xd0,0x91,0x35,0xa8,0x9e,0x4c,0x4b,0x6f,0x10,0x8a,0x2f,0x7d);
static const ble_uuid128_t characteristic = BLE_UUID128_INIT(
    0x01,0xe6,0x72,0x4b,0xd0,0x91,0x35,0xa8,0x9e,0x4c,0x4b,0x6f,0x11,0x8a,0x2f,0x7d);
static uint8_t own_type;
static void advertise(void);

/* Connection metadata only: no addresses, nonces, PINs or key material. */
static void security_status(uint16_t handle)
{
    struct ble_gap_conn_desc d;
    static const uint8_t zero[6];
    if (ble_gap_conn_find(handle, &d)) return;
    printf("[NAN-BLE] security encrypted=%u bonded=%u authenticated=%u key_size=%u "
           "our_type=%u peer_type=%u peer_rpa=%u peer_zero=%u\n",
           d.sec_state.encrypted, d.sec_state.bonded, d.sec_state.authenticated,
           d.sec_state.key_size, d.our_ota_addr.type, d.peer_ota_addr.type,
           BLE_ADDR_IS_RPA(&d.peer_ota_addr), !memcmp(d.peer_ota_addr.val, zero, 6));
}

static int access_value(uint16_t connection, uint16_t attr,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr; (void)arg;
    struct ble_gap_conn_desc desc;
    if (!ble_store_littlefs_ready() || ble_gap_conn_find(connection, &desc) ||
        !desc.sec_state.encrypted || desc.sec_state.key_size != 16)
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t command;
        if (OS_MBUF_PKTLEN(ctxt->om) != 1 || os_mbuf_copydata(ctxt->om, 0, 1, &command) || command != 1)
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        return green2_nan_oob_request() ? BLE_ATT_ERR_UNLIKELY : 0;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t packet[NAN_OOB_PACKET_SIZE];
        green2_nan_oob_read(packet);
        int rc = os_mbuf_append(ctxt->om, packet, sizeof(packet));
        mbedtls_platform_zeroize(packet, sizeof(packet));
        printf("[NAN-BLE] encrypted credential read rc=%d\n", rc);
        return rc ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}
static const struct ble_gatt_svc_def services[] = {
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &service.u,
      .characteristics = (struct ble_gatt_chr_def[]) {
          { .uuid = &characteristic.u, .access_cb = access_value,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE |
                     BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_WRITE_ENC,
            .min_key_size = 16 }, {0} } }, {0}
};
static int gap_event(struct ble_gap_event *e, void *arg)
{
    (void)arg;
    switch (e->type) {
    case BLE_GAP_EVENT_CONNECT:
        printf("[NAN-BLE] connect status=%d\n", e->connect.status);
        if (e->connect.status) advertise();
        else security_status(e->connect.conn_handle);
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        printf("[NAN-BLE] disconnect reason=%d\n", e->disconnect.reason);
        advertise();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE: advertise(); break;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        /* Removing an ASK authorization may discard only the phone's bond.
         * Permit this connected peer to replace its stale Just Works bond,
         * with no downgrade from SC / a 128-bit encryption key. */
        struct ble_gap_conn_desc desc;
        static const uint8_t zero[6];
        const struct ble_gap_repeat_pairing *pairing = &e->repeat_pairing;
        if (!ble_store_littlefs_ready() || !pairing->new_sc ||
            pairing->new_key_size != 16 || !pairing->new_bonding ||
            (pairing->cur_authenticated && !pairing->new_authenticated) ||
            ble_gap_conn_find(pairing->conn_handle, &desc) ||
            !memcmp(desc.peer_ota_addr.val, zero, 6)) {
            printf("[NAN-BLE] repeat pairing rejected\n");
            return BLE_GAP_REPEAT_PAIRING_IGNORE;
        }
        int rc = ble_store_util_delete_peer(&desc.peer_id_addr);
        printf("[NAN-BLE] repeat pairing replace rc=%d\n", rc);
        return rc ? BLE_GAP_REPEAT_PAIRING_IGNORE : BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    case BLE_GAP_EVENT_ENC_CHANGE:
        printf("[NAN-BLE] encryption status=%d store=%u\n", e->enc_change.status, ble_store_littlefs_ready());
        security_status(e->enc_change.conn_handle);
        if (!ble_store_littlefs_ready())
            ble_gap_terminate(e->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        break;
    default: break;
    }
    return 0;
}
static void advertise(void)
{
    if (!ble_store_littlefs_ready()) return;
    struct ble_hs_adv_fields fields = {0}, response = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&service;
    fields.num_uuids128 = 1; fields.uuids128_is_complete = 1;
    const char *name = ble_svc_gap_device_name();
    response.name = (uint8_t *)name; response.name_len = strlen(name); response.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (!rc) rc = ble_gap_adv_rsp_set_fields(&response);
    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND; params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = 160; params.itvl_max = 240; /* 100–150 ms */
    if (!rc) rc = ble_gap_adv_start(own_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    printf("[NAN-BLE] advertising rc=%d\n", rc);
}
static void synced(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (!rc) rc = ble_hs_id_infer_auto(0, &own_type);
    if (!rc) rc = ble_store_littlefs_on_sync();
    printf("[NAN-BLE] host sync rc=%d\n", rc);
    if (!rc) advertise();
}
static void reset(int reason) { printf("[NAN-BLE] host reset reason=%d\n", reason); }
static void host_task(void *arg) { (void)arg; nimble_port_run(); vTaskDelete(NULL); }
void green2_nan_ble_start(void)
{
    extern void nimble_platform_controller_bringup(void);
    extern void nimble_platform_transport_start_rx(void);
    extern int ble_store_lfs_init(void);
    nimble_platform_controller_bringup();
    nimble_port_init();
    if (ble_store_lfs_init()) return;
    ble_svc_gap_init(); ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(services);
    if (!rc) rc = ble_gatts_add_svcs(services);
    if (rc) { printf("[NAN-BLE] GATT setup failed rc=%d\n", rc); return; }
    /* A dedicated service UUID distinguishes this from the BLE benchmark. */
    ble_svc_gap_device_name_set("Green2 NAN BLE");
    ble_hs_cfg.sync_cb = synced; ble_hs_cfg.reset_cb = reset;
    ble_hs_cfg.sm_bonding = 1; ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    nimble_platform_transport_start_rx();
    nimble_port_freertos_init(host_task);
}
