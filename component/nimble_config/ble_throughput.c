/*
 * ble_throughput.c — cross-platform NimBLE BLE throughput test (bl602/bl616 + amebad).
 * See ble_throughput.h for the model. Data flows peripheral -> central via notifications;
 * role is chosen at runtime with `bletp central [secs]` / `bletp peripheral`.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"

#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_mbuf.h"
#include "os/os_mbuf.h"

#include "ble_throughput.h"

/* nimble_app.c helpers (shared host) */
extern uint8_t nimble_own_addr_type(void);
extern void    nimble_restart_advertise(void);

#define TP_MAX_PAYLOAD    244U        /* one DLE PDU (fits a single msys block) */
#define TP_DEFAULT_SECS   10U
#define TP_PREF_MTU       247U        /* -> up to 244-byte notifications */
#define CCCD_UUID16       0x2902U

/* Throughput 128-bit service + its NOTIFY characteristic (bytes are LSB-first). */
static const ble_uuid128_t tp_svc_uuid = BLE_UUID128_INIT(
    0xf0, 0xff, 0x00, 0x00, 0x54, 0x50, 0x42, 0x54, 0x99, 0x99, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66);
static const ble_uuid128_t tp_chr_uuid = BLE_UUID128_INIT(
    0xf1, 0xff, 0x00, 0x00, 0x54, 0x50, 0x42, 0x54, 0x99, 0x99, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66);

static uint16_t s_tp_val_handle;      /* peripheral's NOTIFY char value handle (filled by add_svcs) */

/* ===== peripheral (TX) state ===== */
static uint16_t     s_p_conn = BLE_HS_CONN_HANDLE_NONE;
static volatile int s_tx_run;
static TaskHandle_t s_tx_task;

/* ===== central (RX) state ===== */
static volatile int s_is_central;
static uint16_t     s_c_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t     s_c_svc_end;
static uint16_t     s_c_val_handle;
static uint16_t     s_c_cccd_handle;
static uint32_t     s_secs = TP_DEFAULT_SECS;
static volatile uint64_t s_rx_total;
static volatile int s_rx_active;
static TaskHandle_t s_win_task;

/* bits*1000/ms -> print as "N.NNN Mbps" (kbps = bytes*8/ms). */
static void tp_report(const char *who, uint64_t bytes, uint32_t ms)
{
    uint32_t kbps = (ms > 0) ? (uint32_t)((bytes * 8ULL) / ms) : 0;
    printf("[BLE-TP] %s: %lu bytes in %lu ms = %lu.%03lu Mbps (%lu kbps)\r\n",
           who, (unsigned long)bytes, (unsigned long)ms,
           (unsigned long)(kbps / 1000), (unsigned long)(kbps % 1000), (unsigned long)kbps);
}

/* ================= GATT service (peripheral side) ================= */
static int
tp_chr_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)ctxt; (void)arg;
    return 0;   /* notify-only; no read/write payload */
}

static const struct ble_gatt_svc_def tp_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &tp_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &tp_chr_uuid.u,
                .access_cb = tp_chr_access,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_tp_val_handle,
            },
            { 0 }
        },
    },
    { 0 }
};

int ble_tp_gatt_register(void)
{
    int rc = ble_gatts_count_cfg(tp_gatt_svcs);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(tp_gatt_svcs);
    }
    return rc;
}

/* ================= peripheral: blast notifications ================= */
static void
tp_tx_task(void *arg)
{
    static uint8_t buf[TP_MAX_PAYLOAD];
    uint16_t mtu;
    int      payload;
    uint32_t t0, ms;
    uint64_t total = 0;

    (void)arg;
    memset(buf, 0xA5, sizeof buf);
    mtu = ble_att_mtu(s_p_conn);
    payload = (mtu > 3) ? (mtu - 3) : 20;
    if (payload > (int)TP_MAX_PAYLOAD) {
        payload = TP_MAX_PAYLOAD;
    }
    printf("[BLE-TP] peripheral TX blast (mtu=%u payload=%d)\r\n", (unsigned)mtu, payload);

    t0 = xTaskGetTickCount();
    while (s_tx_run) {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, payload);
        if (om == NULL) {                 /* msys pool momentarily empty -> let it drain */
            vTaskDelay(1);
            continue;
        }
        if (ble_gatts_notify_custom(s_p_conn, s_tp_val_handle, om) == 0) {
            total += (uint32_t)payload;   /* om consumed by NimBLE */
        } else {
            vTaskDelay(1);                /* controller ACL buffers full -> back off */
        }
    }
    ms = (xTaskGetTickCount() - t0) * 1000U / configTICK_RATE_HZ;
    tp_report("peripheral TX", total, ms);
    s_tx_task = NULL;
    vTaskDelete(NULL);
}

void ble_tp_peripheral_event(struct ble_gap_event *event)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_p_conn = event->connect.conn_handle;
        }
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_tp_val_handle) {
            if (event->subscribe.cur_notify && !s_tx_run) {
                s_tx_run = 1;
                if (xTaskCreate(tp_tx_task, "bletp_tx", 1024, NULL,
                                tskIDLE_PRIORITY + 2, &s_tx_task) != pdPASS) {
                    s_tx_run = 0;
                    printf("[BLE-TP] TX task create failed\r\n");
                }
            } else if (!event->subscribe.cur_notify) {
                s_tx_run = 0;             /* central unsubscribed */
            }
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        s_tx_run = 0;
        s_p_conn = BLE_HS_CONN_HANDLE_NONE;
        break;
    default:
        break;
    }
}

/* ================= central: scan -> connect -> negotiate -> subscribe -> measure ========= */
static int tp_central_gap_event(struct ble_gap_event *event, void *arg);

static void
tp_central_revert(void)
{
    s_is_central = 0;
    s_c_conn = BLE_HS_CONN_HANDLE_NONE;
    s_rx_active = 0;
    nimble_restart_advertise();          /* back to the default peripheral role */
}

/* measurement window: sleep `secs`, report goodput, drop the link (-> revert on DISCONNECT). */
static void
tp_window_task(void *arg)
{
    uint32_t t0 = xTaskGetTickCount();
    uint32_t ms;

    (void)arg;
    s_rx_total = 0;
    s_rx_active = 1;
    vTaskDelay(pdMS_TO_TICKS(s_secs * 1000U));
    s_rx_active = 0;
    ms = (xTaskGetTickCount() - t0) * 1000U / configTICK_RATE_HZ;
    tp_report("central RX (goodput)", s_rx_total, ms);

    if (s_c_conn != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_c_conn, BLE_ERR_REM_USER_CONN_TERM);
    }
    s_win_task = NULL;
    vTaskDelete(NULL);
}

/* CCCD discovery -> subscribe */
static int
tp_disc_dsc_cb(uint16_t conn, const struct ble_gatt_error *error,
               uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg)
{
    (void)chr_val_handle; (void)arg;
    if (error->status == 0 && dsc != NULL &&
        ble_uuid_u16(&dsc->uuid.u) == CCCD_UUID16) {
        s_c_cccd_handle = dsc->handle;
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (s_c_cccd_handle != 0) {
            uint8_t v[2] = { 0x01, 0x00 };      /* enable notifications */
            int rc = ble_gattc_write_flat(conn, s_c_cccd_handle, v, sizeof v,
                                          NULL, NULL);
            printf("[BLE-TP] subscribe (cccd=0x%04x) rc=%d -> measuring %lus\r\n",
                   s_c_cccd_handle, rc, (unsigned long)s_secs);
            if (rc == 0) {
                /* Data-length extension is safe + helps; the 2M-PHY switch was destabilising the
                 * coex-contended AmebaD link (dropped ~2 s in), so it's left on the default 1M. */
                (void)ble_gap_set_data_len(conn, 251, 2120);
                xTaskCreate(tp_window_task, "bletp_win", 768, NULL,
                            tskIDLE_PRIORITY + 2, &s_win_task);
            } else {
                tp_central_revert();
            }
        } else {
            printf("[BLE-TP] no CCCD found\r\n");
            ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
        }
    }
    return 0;
}

/* characteristic discovery -> find CCCD */
static int
tp_disc_chr_cb(uint16_t conn, const struct ble_gatt_error *error,
               const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;
    if (error->status == 0 && chr != NULL) {
        s_c_val_handle = chr->val_handle;
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (s_c_val_handle != 0) {
            ble_gattc_disc_all_dscs(conn, s_c_val_handle, s_c_svc_end,
                                    tp_disc_dsc_cb, NULL);
        } else {
            printf("[BLE-TP] TP characteristic not found\r\n");
            ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
        }
    }
    return 0;
}

/* service discovery -> find the char */
static int
tp_disc_svc_cb(uint16_t conn, const struct ble_gatt_error *error,
               const struct ble_gatt_svc *service, void *arg)
{
    (void)arg;
    if (error->status == 0 && service != NULL) {
        s_c_svc_end = service->end_handle;
        ble_gattc_disc_chrs_by_uuid(conn, service->start_handle, service->end_handle,
                                    &tp_chr_uuid.u, tp_disc_chr_cb, NULL);
        return 0;
    }
    if (error->status != 0 && error->status != BLE_HS_EDONE) {
        printf("[BLE-TP] TP service not found (status=%d)\r\n", error->status);
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    }
    return 0;
}

static int
tp_mtu_cb(uint16_t conn, const struct ble_gatt_error *error,
          uint16_t mtu, void *arg)
{
    (void)arg;
    printf("[BLE-TP] MTU=%u (status=%d); discovering service\r\n", mtu, error->status);
    ble_gattc_disc_svc_by_uuid(conn, &tp_svc_uuid.u, tp_disc_svc_cb, NULL);
    return 0;
}

/* returns 1 if the adv carries our 16-bit throughput marker */
static int
tp_adv_has_marker(const uint8_t *data, uint8_t len)
{
    struct ble_hs_adv_fields f;
    int i;
    if (ble_hs_adv_parse_fields(&f, data, len) != 0) {
        return 0;
    }
    for (i = 0; i < f.num_uuids16; i++) {
        if (ble_uuid_u16(&f.uuids16[i].u) == BLE_TP_ADV_UUID16) {
            return 1;
        }
    }
    return 0;
}

static int
tp_central_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        int marker = tp_adv_has_marker(event->disc.data, event->disc.length_data);
        printf("[BLE-TP] disc %02x:%02x:%02x:%02x:%02x:%02x rssi=%d type=%d len=%u marker=%d\r\n",
               event->disc.addr.val[5], event->disc.addr.val[4], event->disc.addr.val[3],
               event->disc.addr.val[2], event->disc.addr.val[1], event->disc.addr.val[0],
               event->disc.rssi, event->disc.event_type, event->disc.length_data, marker);
        if (marker) {
            struct ble_gap_conn_params cp;
            ble_gap_disc_cancel();
            memset(&cp, 0, sizeof cp);
            cp.scan_itvl = 0x0010; cp.scan_window = 0x0010;
            cp.itvl_min = 24; cp.itvl_max = 24;      /* 30 ms — coex-tolerant */
            cp.latency = 0; cp.supervision_timeout = 600;   /* 6 s */
            printf("[BLE-TP] peer found -> connecting\r\n");
            if (ble_gap_connect(nimble_own_addr_type(), &event->disc.addr, 10000,
                                &cp, tp_central_gap_event, NULL) != 0) {
                tp_central_revert();
            }
        }
        return 0;
    }

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            printf("[BLE-TP] connect failed (status=%d)\r\n", event->connect.status);
            tp_central_revert();
            return 0;
        }
        s_c_conn = event->connect.conn_handle;
        s_c_val_handle = 0;
        s_c_cccd_handle = 0;
        printf("[BLE-TP] connected (handle %d) - negotiating\r\n", s_c_conn);
        /* MTU exchange first, then discovery from tp_mtu_cb. PHY/DLE tuning is deferred to after
         * subscribe (tp_start_measure) so those LL control PDUs don't destabilise a fresh, coex-
         * contended link before the GATT setup round-trips have completed. */
        (void)ble_gattc_exchange_mtu(s_c_conn, tp_mtu_cb, NULL);
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        printf("[BLE-TP] central disconnected (reason 0x%x)\r\n",
               event->disconnect.reason);
        tp_central_revert();
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        if (s_rx_active) {
            s_rx_total += OS_MBUF_PKTLEN(event->notify_rx.om);
        }
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (s_is_central && s_c_conn == BLE_HS_CONN_HANDLE_NONE) {
            printf("[BLE-TP] scan ended without a peer; back to peripheral\r\n");
            tp_central_revert();
        }
        return 0;

    default:
        return 0;
    }
}

void ble_tp_start_central(uint32_t secs)
{
    struct ble_gap_disc_params dp;

    s_secs = (secs > 0) ? secs : TP_DEFAULT_SECS;
    s_is_central = 1;
    (void)ble_gap_adv_stop();                    /* leave the peripheral role */
    (void)ble_att_set_preferred_mtu(TP_PREF_MTU);

    memset(&dp, 0, sizeof dp);
    dp.itvl = 0x0010;
    dp.window = 0x0010;
    dp.passive = 0;
    dp.filter_duplicates = 1;
    if (ble_gap_disc(nimble_own_addr_type(), 15000, &dp, tp_central_gap_event, NULL) != 0) {
        printf("[BLE-TP] scan start failed\r\n");
        tp_central_revert();
    } else {
        printf("[BLE-TP] central: scanning for a throughput peer ...\r\n");
    }
}

void ble_tp_start_peripheral(void)
{
    s_tx_run = 0;
    if (s_c_conn != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_c_conn, BLE_ERR_REM_USER_CONN_TERM);
    }
    s_is_central = 0;
    nimble_restart_advertise();
    printf("[BLE-TP] peripheral: advertising; will blast on subscribe\r\n");
}

/* ================= console command ================= */
static BaseType_t
cmd_bletp(char *out, size_t outlen, const char *cmd)
{
    BaseType_t plen;
    const char *p = FreeRTOS_CLIGetParameter(cmd, 1, &plen);

    if (p != NULL && plen == 7 && strncmp(p, "central", 7) == 0) {
        BaseType_t slen;
        const char *s = FreeRTOS_CLIGetParameter(cmd, 2, &slen);
        uint32_t secs = (s != NULL) ? (uint32_t)atoi(s) : TP_DEFAULT_SECS;
        ble_tp_start_central(secs);
        snprintf(out, outlen, "bletp: central role, scanning for peer (%lus window)\r\n",
                 (unsigned long)(secs ? secs : TP_DEFAULT_SECS));
    } else if (p != NULL && plen == 10 && strncmp(p, "peripheral", 10) == 0) {
        ble_tp_start_peripheral();
        snprintf(out, outlen, "bletp: peripheral role, advertising (blast on subscribe)\r\n");
    } else {
        snprintf(out, outlen, "usage: bletp central [secs] | bletp peripheral\r\n");
    }
    return pdFALSE;
}

void ble_tp_cli_register(void)
{
    static const CLI_Command_Definition_t kBleTpCmd = {
        "bletp",
        "bletp central [secs]|peripheral:\r\n BLE throughput test (peripheral notifies central)\r\n",
        cmd_bletp,
        -1
    };
    FreeRTOS_CLIRegisterCommand(&kBleTpCmd);
}
