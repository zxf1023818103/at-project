/*
 * transport_bflb.c — NimBLE custom HCI transport (LL/controller side) over the
 * Bouffalo on-chip BLE controller. BLE_TRANSPORT_LL=custom.
 *
 * Unlike the AmebaD port (H4 byte stream over a UART), the Bouffalo controller
 * exposes a STRUCTURED on-chip HCI API (blecontroller/btblecontroller ->
 * hci_onchip.h): it takes already-parsed command/ACL packets via
 * bt_onchiphci_send(), and delivers already-parsed events/ACL back through a
 * registered bt_hci_recv_cb() callback. So there is NO H4 state machine and NO
 * RX polling task here:
 *   - TX (host->controller): ble_transport_to_ll_{cmd,acl}_impl() unpack the
 *     NimBLE HCI buffer into an hci_pkt_struct and call bt_onchiphci_send().
 *   - RX (controller->host): bflb_hci_rx_cb() (controller-task context) rebuilds
 *     a standard HCI event/ACL packet from (pkt_type, src_id, param, param_len)
 *     and hands it to the host via ble_transport_to_hs_{evt,acl}().
 * The event-reconstruction byte layout mirrors the SDK's bl_hci_wrapper.c
 * (bl_packet_to_host): CMD_COMPLETE/CMD_STATUS/LE_META/plain-EVT.
 *
 * The on-chip controller is brought up here (ble/btble_controller_init) from
 * ble_transport_ll_init(), which NimBLE sysinit calls during nimble_port_init().
 */
#include <string.h>
#include "os/os.h"
#include "nimble/transport.h"
#include "nimble/transport_impl.h"
#include "FreeRTOS.h"
#include "task.h"

/* Bouffalo controller: the on-chip HCI API + the chip-specific controller init.
 * The include path selects the right hci_onchip.h (blecontroller/ble_inc for
 * BL602, btblecontroller/btble_inc for BL616/BL616CL). */
#if defined(BL602)
#include "ble_lib_api.h"
#else
#include "btble_lib_api.h"
#endif
#include "hci_onchip.h"

/* HCI event codes (host format). */
#define HCI_EVT_CMD_COMPLETE   0x0E
#define HCI_EVT_CMD_STATUS     0x0F
#define HCI_EVT_LE_META        0x3E
#define HCI_EVT_VENDOR         0xFF
#define HCI_LE_SUBEV_ADV_RPT   0x02
#define HCI_LE_SUBEV_EXT_ADV   0x0D

static inline uint16_t
le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

/* ===== RX: controller -> host =========================================== */
static void
bflb_hci_rx_cb(uint8_t pkt_type, uint16_t src_id, uint8_t *param, uint8_t param_len)
{
    uint8_t *evt;
    int discardable;

    switch (pkt_type) {
    case BT_HCI_CMD_CMP_EVT:
        /* Command Complete: [0x0E, 3+plen, num_pkts=1, opcode(2 LE), params] */
        evt = ble_transport_alloc_evt(0);
        if (evt == NULL) {
            return;
        }
        evt[0] = HCI_EVT_CMD_COMPLETE;
        evt[1] = (uint8_t)(3 + param_len);
        evt[2] = 1;                          /* num HCI command packets */
        evt[3] = (uint8_t)(src_id & 0xFF);   /* opcode LE */
        evt[4] = (uint8_t)(src_id >> 8);
        if (param_len) {
            memcpy(&evt[5], param, param_len);
        }
        ble_transport_to_hs_evt(evt);
        break;

    case BT_HCI_CMD_STAT_EVT:
        /* Command Status: [0x0F, 4, status, num_pkts=1, opcode(2 LE)] */
        evt = ble_transport_alloc_evt(0);
        if (evt == NULL) {
            return;
        }
        evt[0] = HCI_EVT_CMD_STATUS;
        evt[1] = 4;
        evt[2] = (param != NULL) ? param[0] : 0; /* status */
        evt[3] = 1;                              /* num HCI command packets */
        evt[4] = (uint8_t)(src_id & 0xFF);       /* opcode LE */
        evt[5] = (uint8_t)(src_id >> 8);
        ble_transport_to_hs_evt(evt);
        break;

    case BT_HCI_LE_EVT:
        /* LE Meta: [0x3E, plen, params] (params[0] = subevent code). Adv reports
         * are discardable so they can be dropped under buffer pressure. */
        discardable = (param_len > 0 &&
                       (param[0] == HCI_LE_SUBEV_ADV_RPT ||
                        param[0] == HCI_LE_SUBEV_EXT_ADV)) ? 1 : 0;
        evt = ble_transport_alloc_evt(discardable);
        if (evt == NULL) {
            return;
        }
        evt[0] = HCI_EVT_LE_META;
        evt[1] = param_len;
        if (param_len) {
            memcpy(&evt[2], param, param_len);
        }
        ble_transport_to_hs_evt(evt);
        break;

    case BT_HCI_EVT:
        /* Plain HCI event: [src_id (evt code), plen, params] */
        evt = ble_transport_alloc_evt(0);
        if (evt == NULL) {
            return;
        }
        evt[0] = (uint8_t)src_id;
        evt[1] = param_len;
        if (param_len) {
            memcpy(&evt[2], param, param_len);
        }
        ble_transport_to_hs_evt(evt);
        break;

    case BT_HCI_ACL_DATA: {
        /* Controller hands raw ACL; bt_onchiphci_handle_rx_acl() formats a full
         * HCI ACL packet ([handle+flags, len, data]) and returns its length.
         * Serialized in the controller task, so a static staging buffer is safe. */
        static uint8_t acl_tmp[4 + 512];
        struct os_mbuf *om;
        uint16_t n;

        n = (uint16_t)bt_onchiphci_handle_rx_acl(param, acl_tmp);
        if (n == 0) {
            return;
        }
        om = ble_transport_alloc_acl_from_ll();
        if (om == NULL) {
            return;
        }
        if (os_mbuf_append(om, acl_tmp, n) != 0) {
            os_mbuf_free_chain(om);
            return;
        }
        ble_transport_to_hs_acl(om);
        break;
    }

    default:
        /* SYNC_DATA / DBG_EVT (BL616) and anything else: not handled. */
        break;
    }
}

/* ===== TX: host -> controller =========================================== */
int
ble_transport_to_ll_cmd_impl(void *buf)
{
    uint8_t *p = buf;                 /* [opcode(2 LE), param_len(1), params...] */
    hci_pkt_struct pkt;

    pkt.p.hci_cmd.opcode = le16(p);
    pkt.p.hci_cmd.param_len = p[2];
    pkt.p.hci_cmd.params = &p[3];

    (void)bt_onchiphci_send(BT_HCI_CMD, 0x00, &pkt);
    ble_transport_free(buf);
    return 0;
}

int
ble_transport_to_ll_acl_impl(struct os_mbuf *om)
{
    /* [handle+flags(2 LE), len(2 LE), data...]. Serialized in the host task, so a
     * static staging buffer gives bt_onchiphci_send() a contiguous payload. */
    static uint8_t acl_tx[4 + 512];
    uint16_t total, handle_flags, dlen;
    hci_pkt_struct pkt;

    total = OS_MBUF_PKTLEN(om);
    if (total < 4 || total > sizeof(acl_tx)) {
        os_mbuf_free_chain(om);
        return 0;
    }
    os_mbuf_copydata(om, 0, total, acl_tx);
    handle_flags = le16(&acl_tx[0]);
    dlen = le16(&acl_tx[2]);

    pkt.p.acl_data.conhdl = (uint16_t)(handle_flags & 0x0FFF);
    pkt.p.acl_data.pb_bc_flag = (uint8_t)((handle_flags >> 12) & 0x000F);
    pkt.p.acl_data.len = dlen;
    pkt.p.acl_data.buffer = &acl_tx[4];

    (void)bt_onchiphci_send(BT_HCI_ACL_DATA, pkt.p.acl_data.conhdl, &pkt);
    os_mbuf_free_chain(om);
    return 0;
}

/* ===== Init: called by NimBLE sysinit (nimble_port_init -> ble_transport_init
 * -> ble_transport_ll_init). Brings the on-chip controller up and registers our
 * RX callback. ================================================================ */
void
ble_transport_ll_init(void)
{
#if defined(BL602)
    ble_controller_init(configMAX_PRIORITIES - 1);
#else
    btble_controller_init(configMAX_PRIORITIES - 1);
#endif
    (void)bt_onchiphci_interface_init(bflb_hci_rx_cb);
}
