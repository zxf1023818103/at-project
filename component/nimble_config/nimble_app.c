/*
 * nimble_app.c — NimBLE host bring-up + a sample GATT peripheral for AmebaD KM4.
 *
 * amebad_nimble_start() (called from app_km4/main.c under AT_PROJECT_NIMBLE, after
 * wifi_on):
 *   1. bt_dtm_bringup()  — controller up over our HCI (patch download + HCI reset)
 *   2. nimble_port_init() — NimBLE port + sysinit (-> ble_transport_ll_init)
 *   3. register GAP/GATT services + a sample custom service
 *   4. start the HCI RX task + the host task
 * On host sync we start connectable advertising as "amebad-nimble". A phone
 * (nRF Connect) can scan, connect, and read/write the sample characteristic.
 *
 * P4 will add BLE Mesh; P5 the WiFi/BT coex glue.
 */
#include <string.h>
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <stdio.h>   /* printf */

extern void amebad_nimble_coex_init(void);
extern void amebad_nimble_coex_set_conn(int connected); /* coex_amebad.c: BLE-active hint -> WiFi */
extern void bt_dtm_bringup(void);                   /* hci_dtm.c: controller bring-up */
extern void nimble_amebad_transport_start_rx(void); /* transport_amebad.c: HCI RX task */
extern void amebad_nimble_sm_crypto_selftest(void); /* wolfcrypt_sm_glue.c: SM crypto KAT */

#define NIMBLE_DEV_NAME   "amebad-nimble"

/* Sample 128-bit service 0x59462f12-9543-9999-12c8-58b459a2712d with one
 * read/write characteristic (...2d -> ...2e). */
static const ble_uuid128_t svc_uuid =
    BLE_UUID128_INIT(0x2d, 0x71, 0xa2, 0x59, 0xb4, 0x58, 0xc8, 0x12,
                     0x99, 0x99, 0x43, 0x95, 0x12, 0x2f, 0x46, 0x59);
static const ble_uuid128_t chr_uuid =
    BLE_UUID128_INIT(0x2e, 0x71, 0xa2, 0x59, 0xb4, 0x58, 0xc8, 0x12,
                     0x99, 0x99, 0x43, 0x95, 0x12, 0x2f, 0x46, 0x59);

static uint8_t sample_val;
static uint8_t own_addr_type;

static void nimble_advertise(void);

static int
sample_chr_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return os_mbuf_append(ctxt->om, &sample_val, sizeof sample_val) == 0 ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len >= 1) {
            os_mbuf_copydata(ctxt->om, 0, 1, &sample_val);
            printf("[NimBLE] characteristic written: 0x%02x\n\r", sample_val);
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &chr_uuid.u,
                .access_cb = sample_chr_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
            },
            { 0 }
        },
    },
    { 0 }
};

static int
nimble_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        printf("[NimBLE] connect %s (handle %d)\n\r",
                   event->connect.status == 0 ? "OK" : "FAIL", event->connect.conn_handle);
        if (event->connect.status == 0) {
            amebad_nimble_coex_set_conn(1);   /* BLE link up -> hint WiFi coex */
        } else {
            nimble_advertise();   /* failed — keep advertising */
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        printf("[NimBLE] disconnect (reason 0x%x) — re-advertising\n\r",
                   event->disconnect.reason);
        amebad_nimble_coex_set_conn(0);       /* BLE idle -> hint WiFi coex */
        nimble_advertise();
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        nimble_advertise();
        return 0;
    default:
        return 0;
    }
}

static void
nimble_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *name;
    int rc;

    memset(&fields, 0, sizeof fields);
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        printf("[NimBLE] adv_set_fields rc=%d\n\r", rc);
        return;
    }

    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, nimble_gap_event, NULL);
    if (rc != 0) {
        printf("[NimBLE] adv_start rc=%d\n\r", rc);
        return;
    }
    printf("[NimBLE] advertising as '%s'\n\r", name);
}

static void
nimble_on_sync(void)
{
    int rc;
    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        printf("[NimBLE] ensure_addr rc=%d\n\r", rc);
    }
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        printf("[NimBLE] infer_auto rc=%d\n\r", rc);
        return;
    }
    printf("[NimBLE] host synced — controller ready\n\r");
    nimble_advertise();
}

static void
nimble_on_reset(int reason)
{
    printf("[NimBLE] host reset, reason=%d\n\r", reason);
}

static void
nimble_host_task(void *param)
{
    (void)param;
    printf("[NimBLE] host task: nimble_port_run()\n\r");
    nimble_port_run();   /* runs the host eventq forever */
}

void
amebad_nimble_start(void)
{
    int rc;

    /* 0. verify the wolfCrypt-backed SM crypto (AES + AES-CMAC + ECDH P-256) before the host runs */
    amebad_nimble_sm_crypto_selftest();

    /* 1. controller up over our HCI (gnt_bt + trace + patch download + HCI reset) */
    printf("\n\r[NimBLE] controller bring-up (bt_dtm_bringup) ...\n\r");
    bt_dtm_bringup();

    /* 2. NimBLE port + sysinit (-> ble_transport_ll_init inits our H4 SM) */
    printf("[NimBLE] nimble_port_init ...\n\r");
    nimble_port_init();

    /* 3. services: standard GAP/GATT + our sample service, set the device name */
    ble_svc_gap_init();
    ble_svc_gatt_init();
    rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(gatt_svcs);
    }
    if (rc != 0) {
        printf("[NimBLE] gatt svc register rc=%d\n\r", rc);
    }
    ble_svc_gap_device_name_set(NIMBLE_DEV_NAME);

    ble_hs_cfg.sync_cb  = nimble_on_sync;
    ble_hs_cfg.reset_cb = nimble_on_reset;

    amebad_nimble_coex_init();

    /* 4. HCI RX task + host task */
    nimble_amebad_transport_start_rx();
    nimble_port_freertos_init(nimble_host_task);
    printf("[NimBLE] host task created\n\r");
}
