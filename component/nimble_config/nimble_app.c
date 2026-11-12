/*
 * nimble_app.c — NimBLE host bring-up + a sample GATT peripheral, SHARED across
 * platforms (AmebaD KM4 + Bouffalo BL602/BL616/BL616CL).
 *
 * nimble_start() (called from each platform's main after the scheduler is up):
 *   1. amebad_nimble_sm_crypto_selftest() — SM AES/CMAC/ECDH KAT (mbedtls_sm_glue.c)
 *   2. nimble_platform_controller_bringup() — platform hook (AmebaD: bt_dtm_bringup;
 *      Bouffalo: no-op, its on-chip controller comes up in ble_transport_ll_init)
 *   3. nimble_port_init() — NimBLE port + sysinit (-> ble_transport_ll_init)
 *   4. nimble_platform_store_init() — bond store (default RAM; AmebaD overrides -> LFS)
 *   5. register GAP/GATT + a sample custom service, set the device name
 *   6. platform coex + transport-rx hooks, then start the host task
 * On host sync we start connectable advertising. A phone / nRF Connect / bleak can
 * scan, connect, and read/write the sample characteristic.
 *
 * Platform specifics live behind __weak hooks (default bodies below); a platform's
 * nimble_port provides strong overrides (AmebaD: nimble_platform_amebad.c). Bouffalo
 * needs no override — the defaults (no-op bring-up/coex/rx, RAM store, NIMBLE_DEV_NAME)
 * already fit its on-chip, callback-driven transport.
 */
#include <string.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/ram/ble_store_ram.h"
/* Upstream's ble_store_ram.h declares read/write/delete but not the init entry point,
 * even though ble_store_ram.c defines it. GCC 14 rejects the implicit declaration. */
int ble_store_ram_init(void);
#ifdef AT_NIMBLE_COC
#include "ble_coc.h"
#elif defined(AT_NIMBLE_GATT)
#include "ble_gatt_bench.h"
#else
#include "ble_throughput.h"
#endif

extern void amebad_nimble_sm_crypto_selftest(void); /* mbedtls_sm_glue.c: SM crypto KAT */

#ifdef AT_PROJECT_MCUMGR
extern int  smp_bt_register(void);     /* mcumgr_port/smp_bt.c: register the SMP GATT service */
extern void smp_img_ota_init(void);    /* mcumgr_config/smp_img_ota.c: register img + os + fs command groups */
#endif

#ifndef NIMBLE_DEV_NAME
#define NIMBLE_DEV_NAME   "at-nimble"   /* fallback only, if the hostname is somehow empty */
#endif

/* The default BLE advert name is the device hostname (bt-mac-derived "<chip>_<mac>" or the HOSTNAME
 * override), so BLE, DHCP and mDNS all present the same identity. at_netif_hostname() is self-
 * contained (component/freertos-plus-tcp_config/at_netif.c) -- no ops/network needed -- so it is
 * safe here even though BLE can come up before the WiFi netif is registered. */
extern const char *at_netif_hostname(void);

/* ===== Platform hooks — __weak defaults; a platform's nimble_port overrides ===== */
__attribute__((weak)) void nimble_platform_controller_bringup(void) { }
__attribute__((weak)) void nimble_platform_coex_init(void) { }
__attribute__((weak)) void nimble_platform_coex_set_conn(int connected) { (void)connected; }
__attribute__((weak)) void nimble_platform_transport_start_rx(void) { }
__attribute__((weak)) void nimble_platform_transport_stop_rx(void) { }
__attribute__((weak)) void nimble_platform_store_init(void) { ble_store_ram_init(); }
__attribute__((weak)) const char *nimble_platform_device_name(void)
{
#ifdef AT_NIMBLE_COC
    return "BL618DG-CoC";
#elif defined(AT_NIMBLE_GATT)
    return "BL618DG-GATT-NB";
#else
    const char *n = at_netif_hostname();
    return (n != NULL && n[0] != '\0') ? n : NIMBLE_DEV_NAME;
#endif
}

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
static TaskHandle_t s_host_task_h;   /* the "ble" host task, so nimble_stop() can free its stack */

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

/* mcumgr's smp_bt provides the strong version (clears its reassembly buffer); weak no-op when
 * mcumgr isn't linked into the image. Reset on connect/disconnect so a fresh SMP connection never
 * inherits a partial packet from a dropped/aborted upload. */
__attribute__((weak)) void smp_bt_reset_rx(void) { }

static int
nimble_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
#ifdef AT_NIMBLE_COC
    ble_coc_gap_event(event);
#elif defined(AT_NIMBLE_GATT)
    ble_gatt_bench_event(event);
#else
    ble_tp_peripheral_event(event);   /* throughput test: start/stop the notify blast on subscribe */
#endif
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        printf("[NimBLE] connect %s (handle %d)\n\r",
                   event->connect.status == 0 ? "OK" : "FAIL", event->connect.conn_handle);
        if (event->connect.status == 0) {
            smp_bt_reset_rx();                  /* fresh connection -> clean SMP reassembly buffer */
            nimble_platform_coex_set_conn(1);   /* BLE link up -> hint WiFi coex */
        } else {
            nimble_advertise();   /* failed — keep advertising */
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        printf("[NimBLE] disconnect (reason 0x%x) - re-advertising\n\r",
                   event->disconnect.reason);
        smp_bt_reset_rx();                       /* drop any partial packet from this connection */
        nimble_platform_coex_set_conn(0);       /* BLE idle -> hint WiFi coex */
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

#ifdef AT_NIMBLE_COC
    static const ble_uuid128_t coc_marker=BLE_UUID128_INIT(0xf0,0x92,0xcd,0xb3,0xaf,0x91,0xf4,0xba,0x38,0x4e,0xb3,0x97,0x00,0x10,0xf0,0xb1);
#elif defined(AT_NIMBLE_GATT)
    static const ble_uuid128_t coc_marker=BLE_UUID128_INIT(0xf0,0x92,0xcd,0xb3,0xaf,0x91,0xf4,0xba,0x38,0x4e,0xb3,0x97,0x00,0x20,0xf0,0xb1);
#else
    static const ble_uuid16_t tp_marker = BLE_UUID16_INIT(BLE_TP_ADV_UUID16);
#endif

    memset(&fields, 0, sizeof fields);
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    /* 16-bit marker so a `bletp central` peer can find us (see ble_throughput.h). */
#if defined(AT_NIMBLE_COC) || defined(AT_NIMBLE_GATT)
    fields.uuids128=(ble_uuid128_t *)&coc_marker;
    fields.num_uuids128=1;fields.uuids128_is_complete=1;
#else
    fields.uuids16 = (ble_uuid16_t *)&tp_marker;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
#endif
    name = ble_svc_gap_device_name();
#if defined(AT_NIMBLE_COC) || defined(AT_NIMBLE_GATT)
    struct ble_hs_adv_fields scan_rsp={0};
    scan_rsp.name=(uint8_t *)name;scan_rsp.name_len=strlen(name);scan_rsp.name_is_complete=1;
    rc=ble_gap_adv_rsp_set_fields(&scan_rsp);
    if(rc) {printf("[CoC] scan response rc=%d\r\n",rc);return;}
#else
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
#endif

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

/* Accessors for the throughput module (ble_throughput.c). */
uint8_t nimble_own_addr_type(void) { return own_addr_type; }
void    nimble_restart_advertise(void) { nimble_advertise(); }

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
#if defined(AT_NIMBLE_COC) || defined(AT_NIMBLE_GATT)
    /* Keep the CoC fixture separate from Bluedroid's public identity so iOS
     * cannot reuse the other host's GATT handle cache after reflashing. */
    uint8_t fixture_addr[6];
    rc=ble_hs_id_copy_addr(BLE_ADDR_PUBLIC,fixture_addr,NULL);
    if(!rc) {
        #ifdef AT_NIMBLE_GATT
        fixture_addr[0]^=1; /* separate GATT and CoC handle caches */
#endif
        fixture_addr[5]|=0xc0; /* static random, derived from factory identity */
        rc=ble_hs_id_set_rnd(fixture_addr);
    }
    if(rc) {printf("[CoC] random identity failed=%d\r\n",rc);return;}
    own_addr_type=BLE_OWN_ADDR_RANDOM;
    printf("[CoC] static random=%02x:%02x:%02x:%02x:%02x:%02x\r\n",
           fixture_addr[5],fixture_addr[4],fixture_addr[3],fixture_addr[2],fixture_addr[1],fixture_addr[0]);
#endif
    printf("[NimBLE] host synced - controller ready\n\r");
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
    s_host_task_h = xTaskGetCurrentTaskHandle();   /* let nimble_stop() delete us */
    printf("[NimBLE] host task: nimble_port_run()\n\r");
    nimble_port_run();   /* runs the host eventq forever */
}

void
nimble_start(void)
{
    int rc;
    const char *devname;

    /* 0. verify the mbedTLS-backed SM crypto (AES + AES-CMAC + ECDH P-256) before the host runs */
#ifndef AT_NIMBLE_COC_STANDALONE
    amebad_nimble_sm_crypto_selftest();
#endif

    /* 1. controller bring-up (platform hook). AmebaD: bt_dtm_bringup over our HCI.
     *    Bouffalo: no-op — its on-chip controller comes up in ble_transport_ll_init(). */
    printf("\n\r[NimBLE] controller bring-up ...\n\r");
    nimble_platform_controller_bringup();

    /* 2. NimBLE port + sysinit (-> ble_transport_ll_init) */
    printf("[NimBLE] nimble_port_init ...\n\r");
    nimble_port_init();

    /* 2b. Bond store (platform hook). Default RAM; AmebaD overrides -> LittleFS
     * persistent. Must run before the host task, else ble_hs_cfg.store_read_cb stays
     * NULL and IRK restore fails with BLE_HS_ENOTSUP (8). */
    nimble_platform_store_init();

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
#ifdef AT_NIMBLE_COC
    rc = ble_coc_init();
#elif defined(AT_NIMBLE_GATT)
    rc = ble_gatt_bench_init();
#else
    rc = ble_tp_gatt_register();          /* throughput-test NOTIFY service */
#endif
    if (rc != 0) {
        printf("[NimBLE] tp svc register rc=%d\n\r", rc);
    }
#ifdef AT_PROJECT_MCUMGR
    /* Register the SMP GATT service (mcumgr BLE DFU) alongside the sample service, then the
     * img + os command groups. Must happen before the host task starts. */
    smp_bt_register();
    smp_img_ota_init();
#endif
    devname = nimble_platform_device_name();
    if (devname == NULL || devname[0] == '\0') {
        devname = NIMBLE_DEV_NAME;
    }
    ble_svc_gap_device_name_set(devname);

    ble_hs_cfg.sync_cb  = nimble_on_sync;
    ble_hs_cfg.reset_cb = nimble_on_reset;

    nimble_platform_coex_init();

    /* 4. transport RX (platform hook; AmebaD: HCI RX task. Bouffalo: no-op —
     *    callback-driven RX) + the host task */
    nimble_platform_transport_start_rx();
    nimble_port_freertos_init(nimble_host_task);
    printf("[NimBLE] host task created\n\r");
}

/* Tear BLE down to reclaim heap (e.g. for the OTA HTTPS data-plane TLS on AmebaD).
 * Stops advertising, then deletes the host + HCI-RX tasks to free their stacks.
 * One-way: a successful OTA reboots (BLE returns fresh). */
void
nimble_stop(void)
{
    static int stopped = 0;
    if (stopped) {
        return;
    }
    stopped = 1;

    printf("[NimBLE] stopping (free heap before %u)\n\r",
           (unsigned)xPortGetFreeHeapSize());

    (void)ble_gap_adv_stop();               /* stop the radio + the adv state machine */
    nimble_platform_coex_set_conn(0);       /* tell WiFi coex BLE is idle */
    nimble_platform_transport_stop_rx();    /* delete the HCI RX task (AmebaD) */
    if (s_host_task_h != NULL) {
        vTaskDelete(s_host_task_h);         /* delete the host task -> frees its stack */
        s_host_task_h = NULL;
    }

    printf("[NimBLE] stopped (free heap after %u)\n\r",
           (unsigned)xPortGetFreeHeapSize());
}
