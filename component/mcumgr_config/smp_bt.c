/*
 * smp_bt.c - mcumgr SMP-over-BLE transport for AmebaD NimBLE (the "transport/smp_bluetooth" that
 * apache/mynewt-mcumgr documents but ships only in mynewt-core). Registers the standard SMP GATT
 * service so off-the-shelf tools (mcumgr CLI, nRF Connect Device Manager) can do BLE DFU:
 *
 *   service  8D53DC1D-1DB7-4CD3-868B-8A527460AA84
 *   chr      DA2E7828-FBCE-4E01-AE9E-261174997C48   (Write / Write-No-Rsp + Notify + CCCD)
 *
 * Inbound: the client writes SMP request fragments; we concatenate them and, once a full packet
 * (8-byte mgmt header + nh_len payload) has arrived, hand it to mcumgr (mcumgr_smp_process).
 * Outbound: mcumgr's response is fragmented to the ATT MTU and sent as notifications.
 *
 * Built only for AT_ENABLE_MCUMGR. Registered from nimble_app.c (amebad_nimble_start), in the
 * GATT service-registration phase, before the host starts.
 */
#include <string.h>
#include <stdint.h>

#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "host/ble_att.h"

#include "mcumgr_compat.h"    /* defines cbor reader/writer structs; pull in before mgmt.h */
#include "mgmt/mgmt.h"        /* MGMT_ERR_*, MGMT_HDR_SIZE */
#include "at_log.h"

/* UUIDs as little-endian byte arrays (BLE_UUID128_INIT takes LE order). */
static const ble_uuid128_t smp_svc_uuid =
    BLE_UUID128_INIT(0x84, 0xaa, 0x60, 0x74, 0x52, 0x8a, 0x8b, 0x86,
                     0xd3, 0x4c, 0xb7, 0x1d, 0x1d, 0xdc, 0x53, 0x8d);
static const ble_uuid128_t smp_chr_uuid =
    BLE_UUID128_INIT(0x48, 0x7c, 0x99, 0x74, 0x11, 0x26, 0x9e, 0xae,
                     0x01, 0x4e, 0xce, 0xfb, 0x28, 0x78, 0x2e, 0xda);

#define SMP_BT_RX_SZ 1152      /* must be >= MCUMGR_BUF_SZ in mcumgr_compat.c */

static uint16_t s_val_handle;
static uint8_t  s_rx[SMP_BT_RX_SZ];
static size_t   s_rx_len;

/* mcumgr response sink: fragment to the ATT MTU and notify the client. arg = conn handle. */
static int
smp_tx(void *arg, const uint8_t *data, size_t len)
{
    uint16_t conn = (uint16_t) (uintptr_t) arg;
    uint16_t mtu = ble_att_mtu(conn);
    size_t chunk = (mtu > 3) ? (size_t) (mtu - 3) : 20;
    size_t off = 0;

    while (off < len) {
        size_t n = len - off;
        struct os_mbuf *om;
        int rc;

        if (n > chunk) {
            n = chunk;
        }
        om = ble_hs_mbuf_from_flat(data + off, (uint16_t) n);
        if (om == NULL) {
            return MGMT_ERR_ENOMEM;
        }
        /* consumes `om` on success and failure */
        rc = ble_gatts_notify_custom(conn, s_val_handle, om);
        if (rc != 0) {
            log_error("[mcumgr] notify rc=%d", rc);
            return MGMT_ERR_EUNKNOWN;
        }
        off += n;
    }
    return 0;
}

static int
smp_chr_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint16_t n;
    (void)attr; (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    n = OS_MBUF_PKTLEN(ctxt->om);
    if (s_rx_len + n > sizeof s_rx) {
        s_rx_len = 0;                       /* malformed / too large -> drop and resync */
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    os_mbuf_copydata(ctxt->om, 0, n, s_rx + s_rx_len);
    s_rx_len += n;

    /* Process every complete SMP packet that has fully arrived. */
    while (s_rx_len >= MGMT_HDR_SIZE) {
        uint16_t plen = (uint16_t) ((s_rx[2] << 8) | s_rx[3]);   /* nh_len, network order */
        size_t total = (size_t) MGMT_HDR_SIZE + plen;

        if (s_rx_len < total) {
            break;                          /* more fragments coming */
        }
        mcumgr_smp_process(s_rx, total, smp_tx, (void *) (uintptr_t) conn);
        memmove(s_rx, s_rx + total, s_rx_len - total);
        s_rx_len -= total;
    }
    return 0;
}

static const struct ble_gatt_svc_def smp_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &smp_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &smp_chr_uuid.u,
                .access_cb = smp_chr_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP |
                         BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_val_handle,
            },
            { 0 }
        },
    },
    { 0 }
};

/* Drop any half-assembled request (call on disconnect). */
void
smp_bt_reset(void)
{
    s_rx_len = 0;
}

int
smp_bt_register(void)
{
    int rc;

    rc = ble_gatts_count_cfg(smp_svcs);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(smp_svcs);
    }
    if (rc != 0) {
        log_error("[mcumgr] smp_bt register rc=%d", rc);
    } else {
        log_info("[mcumgr] smp_bt service registered");
    }
    return rc;
}
