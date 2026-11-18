/* SPDX-License-Identifier: Apache-2.0
 * NimBLE RAS 1.0 server + on-demand client, one encrypted connection.
 * Mandatory characteristics and Get/ACK; optional Abort (feature bit 2).
 * No real-time, filtering, lost-segment retrieval or vendor PCT extensions.
 */
#include "ble_ras.h"
#include "ras_control.h"
#include "ras_selftest.h"
#include "host/ble_hs.h"
#include "host/ble_cs.h"
#include "nimble/nimble_port.h"
#include <string.h>

enum { FEATURES, DATA, CP, READY, OVERWRITTEN, NUM_CHARS };
enum { CLIENT_IDLE, CLIENT_DATA, CLIENT_ACK };
static struct ras_record local;
static struct ras_rx remote;
/* The board's startup does not clear IRAM NOLOAD sections: init explicitly. */
static struct cs_distance_work work
#ifdef TL3238_ENABLE_CS
    __attribute__((section(".iram_bss"), aligned(16)))
#endif
    ;
static struct ble_ras_status status;
static struct ble_npl_callout timer;
static uint16_t values[NUM_CHARS];
static struct {
    uint16_t conn, offset, pending_indication, ready_counter, overwritten_counter;
    uint8_t subscribed[NUM_CHARS], state, seq, pending_ready, pending_overwritten;
    uint8_t reply[4][3], reply_len[4], head, count, role, wait_ack_started;
    int8_t tx_power;
    uint32_t deadline;
} server;
static struct {
    uint16_t start, end, value[NUM_CHARS], desc_end[NUM_CHARS], cccd[NUM_CHARS];
    uint16_t counter, consumed_counter;
    int previous;
    uint8_t slot, phase, peer_ready, consumed, ready_readable;
    uint32_t features, deadline;
} client;
static const uint16_t char_uuids[NUM_CHARS] = {0x2c14,0x2c16,0x2c17,0x2c18,0x2c19};
static const ble_uuid_any_t ras_uuid = {.u16 = BLE_UUID16_INIT(0x185b)};
static uint32_t now(void) { return ble_npl_time_get(); }
static uint32_t timeout_ticks(void) { return ble_npl_time_ms_to_ticks32(5000); }
static int expired(uint32_t deadline) { return (int32_t)(now() - deadline) >= 0; }
static void kick(void) { ble_npl_callout_reset(&timer, ble_npl_time_ms_to_ticks32(10)); }
static int encrypted(void)
{
    struct ble_gap_conn_desc d;
    return !ble_gap_conn_find(server.conn, &d) && d.sec_state.encrypted;
}
static void error(int rc) { status.last_rc = rc; status.errors++; }
static int client_command(uint8_t op, uint16_t counter)
{
    uint8_t p[3] = {op}; ras_put16(p+1, counter & 0xfff);
    return ble_gattc_write_no_rsp_flat(server.conn, client.value[CP], p, op == 3 ? 1 : 3);
}
static void client_fail(int rc)
{
    error(rc); status.distance.status = rc;
    if (client.phase && (client.features & 4)) (void)client_command(3, 0);
    client.phase = CLIENT_IDLE; client.peer_ready = 0;
}
static void queue_reply(const uint8_t *p, unsigned n)
{
    if (server.count == 4) { error(BLE_HS_ENOMEM); return; }
    unsigned at = (server.head + server.count) & 3;
    memcpy(server.reply[at], p, n); server.reply_len[at] = n; server.count++; kick();
}
static void reply_code(uint8_t code) { const uint8_t p[] = {2,code}; queue_reply(p, sizeof p); }
static void try_get(void)
{
    if (!status.client_ready || client.phase || !client.peer_ready || !local.complete || local.failed ||
        client.counter != (local.counter & 0xfff) ||
        (client.consumed && client.consumed_counter == local.counter)) return;
    memset(&remote, 0, sizeof remote);
    int rc = client_command(0, client.counter);
    if (rc) { client_fail(rc); return; }
    client.phase = CLIENT_DATA; client.deadline = now() + timeout_ticks(); kick();
}
static int access_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr;
    unsigned which = (uintptr_t)arg;
    struct ble_gap_conn_desc d;
    if (ble_gap_conn_find(conn, &d) || !d.sec_state.encrypted) return BLE_ATT_ERR_INSUFFICIENT_ENC;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t value[4] = {4,0,0,0}; unsigned n = 4;
        if (which == READY || which == OVERWRITTEN) {
            ras_put16(value, which == READY ? server.ready_counter : server.overwritten_counter); n = 2;
        } else if (which != FEATURES) return BLE_ATT_ERR_READ_NOT_PERMITTED;
        return os_mbuf_append(ctxt->om, value, n) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
    }
    if (which != CP || ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    if (!(server.subscribed[CP] & 2)) return BLE_ATT_ERR_CCCD_IMPORER_CONF;
    uint8_t p[5], reply[2]; unsigned n = OS_MBUF_PKTLEN(ctxt->om);
    if (n > sizeof p || os_mbuf_copydata(ctxt->om, 0, n, p)) { reply_code(3); return 0; }
    int action = ras_control_request(p, n, server.state, status.available, local.counter, reply);
    if (action == RAS_GET) {
        if (!server.subscribed[DATA]) return BLE_ATT_ERR_CCCD_IMPORER_CONF;
        server.state = RAS_SERVER_SEND; server.offset = 0;
        server.deadline = now() + timeout_ticks(); kick();
    } else {
        if (action == RAS_ACK) { status.available = 0; server.state = RAS_SERVER_IDLE; }
        if (action == RAS_ABORT) {
            server.state = RAS_SERVER_IDLE;
            /* Drop a queued Complete response as well as unsent data segments. */
            server.head = server.count = 0;
        }
        queue_reply(reply, sizeof reply);
    }
    return 0;
}
#define ENC (BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_WRITE_ENC)
static const struct ble_gatt_svc_def services[] = {{
    .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0x185b),
    .characteristics = (const struct ble_gatt_chr_def[]) {
        {.uuid=BLE_UUID16_DECLARE(0x2c14), .access_cb=access_cb, .arg=(void *)FEATURES,
         .flags=BLE_GATT_CHR_F_READ|ENC, .val_handle=&values[FEATURES]},
        {.uuid=BLE_UUID16_DECLARE(0x2c16), .access_cb=access_cb, .arg=(void *)DATA,
         .flags=BLE_GATT_CHR_F_NOTIFY|BLE_GATT_CHR_F_INDICATE|ENC, .val_handle=&values[DATA]},
        {.uuid=BLE_UUID16_DECLARE(0x2c17), .access_cb=access_cb, .arg=(void *)CP,
         .flags=BLE_GATT_CHR_F_WRITE_NO_RSP|BLE_GATT_CHR_F_INDICATE|ENC, .val_handle=&values[CP]},
        {.uuid=BLE_UUID16_DECLARE(0x2c18), .access_cb=access_cb, .arg=(void *)READY,
         .flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_INDICATE|ENC, .val_handle=&values[READY]},
        {.uuid=BLE_UUID16_DECLARE(0x2c19), .access_cb=access_cb, .arg=(void *)OVERWRITTEN,
         .flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_INDICATE|ENC, .val_handle=&values[OVERWRITTEN]},
        {0}
    }
}, {0}};
static int send_value(unsigned which, const uint8_t *p, unsigned n)
{
    if (server.pending_indication) return BLE_HS_EBUSY;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(p, n);
    if (!om) return BLE_HS_ENOMEM;
    int indicate = which != DATA || !(server.subscribed[DATA] & 1);
    /* Set before calling NimBLE: NOTIFY_TX may be delivered synchronously. */
    if (indicate) server.pending_indication = values[which];
    int rc = indicate ? ble_gatts_indicate_custom(server.conn, values[which], om) :
                        ble_gatts_notify_custom(server.conn, values[which], om);
    if (rc && indicate) server.pending_indication = 0;
    return rc;
}
static void pump(struct ble_npl_event *event)
{
    (void)event;
    if (server.conn == BLE_HS_CONN_HANDLE_NONE || !encrypted()) return;
    if (client.phase && expired(client.deadline)) client_fail(BLE_HS_ETIMEOUT);
    if ((server.state == RAS_SERVER_SEND || (server.state == RAS_SERVER_ACK && server.wait_ack_started)) && expired(server.deadline)) {
        server.state = RAS_SERVER_IDLE; error(BLE_HS_ETIMEOUT);
    }
    uint8_t p[244]; int rc = 0;
    if (!server.pending_indication) {
        if (server.count && (server.subscribed[CP] & 2)) {
            unsigned head = server.head;
            rc = send_value(CP, server.reply[head], server.reply_len[head]);
            if (!rc) {
                if (server.reply[head][0] == 0 && server.state == RAS_SERVER_ACK) {
                    server.wait_ack_started = 1; server.deadline = now() + timeout_ticks();
                }
                server.head = (head + 1) & 3; server.count--;
            }
        } else if (server.pending_overwritten && server.subscribed[OVERWRITTEN]) {
            ras_put16(p, server.overwritten_counter); rc = send_value(OVERWRITTEN, p, 2);
            if (!rc) server.pending_overwritten = 0;
        } else if (server.pending_ready && server.subscribed[READY]) {
            ras_put16(p, server.ready_counter); rc = send_value(READY, p, 2);
            if (!rc) server.pending_ready = 0;
        } else if (server.state == RAS_SERVER_SEND) {
            if (!server.subscribed[DATA]) { server.state = RAS_SERVER_IDLE; reply_code(6); }
            else {
                unsigned n = ras_segment(local.body, local.len, server.offset, server.seq,
                                          ble_att_mtu(server.conn), p, sizeof p);
                if (!n) { server.state = RAS_SERVER_IDLE; reply_code(6); }
                else {
                    rc = send_value(DATA, p, n);
                    if (!rc) {
                        server.offset += n - 1; server.seq = (server.seq + 1) & 63;
                        status.tx_segments++; server.deadline = now() + timeout_ticks();
                        if (server.offset == local.len) {
                            server.state = RAS_SERVER_ACK; server.wait_ack_started = 0;
                            uint8_t done[3] = {0}; ras_put16(done+1, local.counter & 0xfff);
                            queue_reply(done, sizeof done);
                        }
                    }
                }
            }
        }
    }
    if (rc && rc != BLE_HS_EBUSY && rc != BLE_HS_ENOMEM && rc != BLE_HS_EALREADY) {
        error(rc); server.state = RAS_SERVER_IDLE;
    }
    if (server.state || client.phase || server.pending_indication ||
        (server.count && server.subscribed[CP]) ||
        (server.pending_ready && server.subscribed[READY]) ||
        (server.pending_overwritten && server.subscribed[OVERWRITTEN]))
        ble_npl_callout_reset(&timer, ble_npl_time_ms_to_ticks32(server.pending_indication ? 100 : 20));
}
static void discovery_failed(int rc)
{
    status.discovering = status.client_ready = 0;
    error(rc);
}
static int read_ready(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    (void)arg;
    if (conn != server.conn) return 0;
    uint8_t p[2];
    if (err->status || !attr || OS_MBUF_PKTLEN(attr->om) != 2 || os_mbuf_copydata(attr->om, 0, 2, p)) {
        error(err->status ? err->status : BLE_HS_EBADDATA); return 0;
    }
    client.counter = ras_get16(p) & 0xfff; client.peer_ready = 1; try_get(); return 0;
}
static int subscribe_next(void);
static int subscribed(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    (void)attr; (void)arg;
    if (conn != server.conn || !status.discovering) return 0;
    if (err->status) { discovery_failed(err->status); return 0; }
    client.slot++;
    int rc = subscribe_next(); if (rc) discovery_failed(rc);
    return 0;
}
static int subscribe_next(void)
{
    if (client.slot == NUM_CHARS) {
        status.discovering = 0; status.client_ready = 1; status.last_rc = 0;
        return client.ready_readable ? ble_gattc_read(server.conn, client.value[READY], read_ready, NULL) : 0;
    }
    const uint8_t cccd[] = {2,0}; /* Indications give ATT-confirmed delivery. */
    return ble_gattc_write_flat(server.conn, client.cccd[client.slot], cccd, 2, subscribed, NULL);
}
static int discover_descriptors(void);
static int descriptor(uint16_t conn, const struct ble_gatt_error *err, uint16_t chr,
                       const struct ble_gatt_dsc *dsc, void *arg)
{
    (void)chr; (void)arg;
    if (conn != server.conn || !status.discovering) return 0;
    if (!err->status) {
        if (ble_uuid_u16(&dsc->uuid.u) == 0x2902) client.cccd[client.slot] = dsc->handle;
    } else if (err->status == BLE_HS_EDONE) {
        if (!client.cccd[client.slot]) { discovery_failed(BLE_HS_ENOENT); return 0; }
        client.slot++;
        int rc = discover_descriptors(); if (rc) discovery_failed(rc);
    } else discovery_failed(err->status);
    return 0;
}
static int discover_descriptors(void)
{
    if (client.slot == NUM_CHARS) { client.slot = DATA; return subscribe_next(); }
    return ble_gattc_disc_all_dscs(server.conn, client.value[client.slot],
                                  client.desc_end[client.slot], descriptor, NULL);
}
static int features_read(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    (void)arg;
    if (conn != server.conn || !status.discovering) return 0;
    uint8_t p[4];
    if (err->status || !attr || OS_MBUF_PKTLEN(attr->om) != 4 || os_mbuf_copydata(attr->om, 0, 4, p)) {
        discovery_failed(err->status ? err->status : BLE_HS_EBADDATA); return 0;
    }
    client.features = ras_get16(p) | (uint32_t)ras_get16(p + 2) << 16;
    client.slot = DATA;
    int rc = discover_descriptors(); if (rc) discovery_failed(rc);
    return 0;
}
static int characteristic(uint16_t conn, const struct ble_gatt_error *err,
                          const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;
    if (conn != server.conn || !status.discovering) return 0;
    if (!err->status) {
        if (client.previous >= 0) client.desc_end[client.previous] = chr->def_handle - 1;
        client.previous = -1;
        for (unsigned i = 0; i < NUM_CHARS; i++) if (ble_uuid_u16(&chr->uuid.u) == char_uuids[i]) {
            unsigned required = i == FEATURES ? BLE_GATT_CHR_PROP_READ : BLE_GATT_CHR_PROP_INDICATE;
            if (i == CP) required |= BLE_GATT_CHR_PROP_WRITE_NO_RSP;
            if ((chr->properties & required) != required || client.value[i]) {
                discovery_failed(BLE_HS_ENOTSUP); return 0;
            }
            client.value[i] = chr->val_handle; client.desc_end[i] = client.end; client.previous = i;
            if (i == READY) client.ready_readable = !!(chr->properties & BLE_GATT_CHR_PROP_READ);
        }
    } else if (err->status == BLE_HS_EDONE) {
        for (unsigned i = 0; i < NUM_CHARS; i++) if (!client.value[i]) {
            discovery_failed(BLE_HS_ENOENT); return 0;
        }
        int rc = ble_gattc_read(conn, client.value[FEATURES], features_read, NULL);
        if (rc) discovery_failed(rc);
    } else discovery_failed(err->status);
    return 0;
}
static int service(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_svc *svc, void *arg)
{
    (void)arg;
    if (conn != server.conn || !status.discovering) return 0;
    if (!err->status) {
        if (client.start) { discovery_failed(BLE_HS_ENOTSUP); return 0; }
        client.start = svc->start_handle; client.end = svc->end_handle;
    } else if (err->status == BLE_HS_EDONE) {
        if (!client.start) { discovery_failed(BLE_HS_ENOENT); return 0; }
        int rc = ble_gattc_disc_all_chrs(conn, client.start, client.end, characteristic, NULL);
        if (rc) discovery_failed(rc);
    } else discovery_failed(err->status);
    return 0;
}
static int mtu_exchanged(uint16_t conn, const struct ble_gatt_error *err, uint16_t mtu, void *arg)
{
    (void)err; (void)mtu; (void)arg;
    if (conn != server.conn || !status.discovering) return 0;
    int rc = ble_gattc_disc_svc_by_uuid(conn, &ras_uuid.u, service, NULL);
    if (rc) discovery_failed(rc);
    return 0;
}
int ble_ras_discover(void)
{
    if (server.conn == BLE_HS_CONN_HANDLE_NONE) return BLE_HS_ENOTCONN;
    if (!encrypted()) return BLE_HS_EAUTHEN;
    if (status.discovering || client.phase) return BLE_HS_EBUSY;
    memset(&client, 0, sizeof client); client.previous = -1;
    status.client_ready = 0; status.discovering = 1;
    int rc = ble_gattc_exchange_mtu(server.conn, mtu_exchanged, NULL);
    if (rc == BLE_HS_EALREADY) return mtu_exchanged(server.conn, NULL, 0, NULL);
    if (rc) discovery_failed(rc);
    return rc;
}
int ble_ras_get(void)
{
    if (server.conn == BLE_HS_CONN_HANDLE_NONE) return BLE_HS_ENOTCONN;
    if (!encrypted()) return BLE_HS_EAUTHEN;
    if (!status.client_ready) return BLE_HS_ENOTSYNCED;
    if (client.phase) return BLE_HS_EBUSY;
    if (!local.complete || local.failed) return BLE_HS_EAGAIN;
    client.consumed = 0;
    if (!client.ready_readable) { try_get(); return client.phase ? 0 : BLE_HS_EAGAIN; }
    return ble_gattc_read(server.conn, client.value[READY], read_ready, NULL);
}
static void incoming(uint16_t handle, const uint8_t *p, unsigned n)
{
    if (!status.client_ready) return;
    if (handle == client.value[READY]) {
        if (n != 2) { client_fail(BLE_HS_EBADDATA); return; }
        client.counter = ras_get16(p) & 0xfff; client.peer_ready = 1; try_get();
    } else if (handle == client.value[OVERWRITTEN]) {
        if (n == 2 && (ras_get16(p) & 0xfff) == client.counter) client_fail(BLE_HS_ENOENT);
    } else if (handle == client.value[DATA]) {
        if (client.phase != CLIENT_DATA) { client_fail(BLE_HS_EBADDATA); return; }
        int rc = ras_rx_segment(&remote, p, n);
        if (rc) { client_fail(rc); return; }
        status.rx_segments++; client.deadline = now() + timeout_ticks();
    } else if (handle == client.value[CP]) {
        if (n == 3 && p[0] == 0 && client.phase == CLIENT_DATA) {
            if (!remote.complete || ras_get16(p+1) != client.counter) { client_fail(RAS_SEQUENCE); return; }
            (void)cs_distance_estimate(&local, remote.body, remote.len, status.offset_mm, &work, &status.distance);
            status.remote_counter = client.counter;
            client.consumed = 1; client.consumed_counter = local.counter;
            int rc = client_command(1, client.counter);
            if (rc) { client_fail(rc); return; }
            client.phase = CLIENT_ACK; client.deadline = now() + timeout_ticks();
        } else if (n == 2 && p[0] == 2) {
            if (client.phase == CLIENT_ACK && p[1] == 1) {
                client.phase = CLIENT_IDLE; client.peer_ready = 0; status.last_rc = 0;
            } else if (client.phase) client_fail(0x100 + p[1]);
        } else if (client.phase) client_fail(BLE_HS_EBADDATA);
    }
}
void ble_ras_reset(void)
{
    ble_npl_callout_stop(&timer);
    memset(&server, 0, sizeof server); server.conn = BLE_HS_CONN_HANDLE_NONE; server.role = 1;
    memset(&client, 0, sizeof client); client.previous = -1;
    ras_record_reset(&local); memset(&remote, 0, sizeof remote);
    status.client_ready = status.discovering = status.available = 0;
    memset(&status.distance, 0, sizeof status.distance); status.distance.status = RAS_INCOMPLETE;
}
void ble_ras_gap_event(struct ble_gap_event *e)
{
    if (e->type == BLE_GAP_EVENT_CONNECT && !e->connect.status) {
        ble_ras_reset(); server.conn = e->connect.conn_handle;
    } else if (e->type == BLE_GAP_EVENT_DISCONNECT) ble_ras_reset();
    else if (e->type == BLE_GAP_EVENT_ENC_CHANGE) {
        struct ble_gap_conn_desc d;
        if (!e->enc_change.status && !ble_gap_conn_find(server.conn, &d) && d.sec_state.encrypted) {
            kick();
            if (d.role == BLE_GAP_ROLE_MASTER && !status.client_ready && !status.discovering)
                (void)ble_ras_discover();
        }
    } else if (e->type == BLE_GAP_EVENT_SUBSCRIBE && e->subscribe.conn_handle == server.conn) {
        for (unsigned i = 0; i < NUM_CHARS; i++) if (values[i] == e->subscribe.attr_handle) {
            server.subscribed[i] = e->subscribe.cur_notify | e->subscribe.cur_indicate << 1;
            if (i == READY && status.available) server.pending_ready = 1;
            if (i == CP && !(server.subscribed[CP] & 2)) {
                server.state = RAS_SERVER_IDLE; server.head = server.count = 0;
            }
        }
        kick();
    } else if (e->type == BLE_GAP_EVENT_NOTIFY_TX && e->notify_tx.conn_handle == server.conn) {
        if (e->notify_tx.indication && server.pending_indication == e->notify_tx.attr_handle && e->notify_tx.status) {
            server.pending_indication = 0;
            if (e->notify_tx.status != BLE_HS_EDONE) { error(e->notify_tx.status); server.state = RAS_SERVER_IDLE; }
        }
        kick();
    } else if (e->type == BLE_GAP_EVENT_NOTIFY_RX && e->notify_rx.conn_handle == server.conn && encrypted()) {
        uint8_t data[244]; unsigned n = OS_MBUF_PKTLEN(e->notify_rx.om);
        /* Ignore notifications belonging to other services (e.g. mcumgr). */
        int ours = 0;
        for (unsigned i = DATA; i < NUM_CHARS; i++) if (client.value[i] == e->notify_rx.attr_handle) ours = 1;
        if (!ours) return;
        if (n > sizeof data || os_mbuf_copydata(e->notify_rx.om, 0, n, data)) client_fail(BLE_HS_EBADDATA);
        else incoming(e->notify_rx.attr_handle, data, n);
    }
}
void ble_ras_cs_event(const struct ble_cs_event *e)
{
    if (e->type == BLE_CS_EVENT_ERROR) {
        status.distance.status = e->error.status;
        return;
    }
    if (e->type == BLE_CS_EVENT_HCI) {
        if (e->hci.subevent == 0x2f) {
            if (e->hci.len != sizeof(struct ble_hci_ev_le_subev_cs_config_complete)) { error(RAS_INVALID); return; }
            const struct ble_hci_ev_le_subev_cs_config_complete *p = (const void *)e->hci.data;
            if (p->action == 1) server.role = p->role;
        } else if (e->hci.subevent == 0x30) {
            if (e->hci.len != sizeof(struct ble_hci_ev_le_subev_cs_proc_enable_complete)) { error(RAS_INVALID); return; }
            const struct ble_hci_ev_le_subev_cs_proc_enable_complete *p = (const void *)e->hci.data;
            if (p->state) {
                server.tx_power = p->selected_tx_power;
                status.distance.status = RAS_INCOMPLETE;
            }
        }
        return;
    }
    if (e->type != BLE_CS_EVENT_SUBEVENT_RESULT) return;
    struct ras_fragment f = {
        .counter=e->result.procedure_counter, .start_acl_event=e->result.start_acl_event,
        .frequency_compensation=e->result.frequency_compensation, .reference_power=e->result.reference_power,
        .config=e->result.config_id, .continuation=e->result.continuation,
        .procedure_status=e->result.procedure_status, .subevent_status=e->result.subevent_status,
        .abort_reason=e->result.abort_reason, .paths=e->result.num_antenna_paths,
        .num_steps=e->result.num_steps, .steps=e->result.steps, .steps_len=e->result.steps_len,
    };
    if (!f.continuation && (!local.active || local.counter != f.counter)) {
        if (status.available) {
            server.overwritten_counter = local.counter & 0xfff; server.pending_overwritten = 1;
            status.overwritten++;
        }
        if (server.state) { server.state = RAS_SERVER_IDLE; reply_code(6); }
        if (client.phase) client_fail(RAS_MISMATCH);
        status.available = 0; client.consumed = 0;
        memset(&status.distance, 0, sizeof status.distance); status.distance.status = RAS_INCOMPLETE;
    }
    int rc = ras_record_append(&local, &f, server.role, server.tx_power);
    if (rc) {
        error(rc); status.available = 0; server.pending_ready = 0;
        status.distance.status = rc;
        if (server.state) { server.state = RAS_SERVER_IDLE; reply_code(6); }
        return;
    }
    if (local.complete) {
        status.available = 1; server.ready_counter = local.counter & 0xfff;
        server.pending_ready = 1; kick(); try_get();
    }
}
int ble_ras_init(void)
{
    memset(&status, 0, sizeof status); memset(&work, 0, sizeof work);
    ble_npl_callout_init(&timer, nimble_port_get_dflt_eventq(), pump, NULL);
    ble_ras_reset();
    int rc = ble_gatts_count_cfg(services);
    if (!rc) rc = ble_gatts_add_svcs(services);
    status.service = !rc; return rc;
}
void ble_ras_status(struct ble_ras_status *out)
{
    status.conn = server.conn; status.local_counter = local.counter; status.bytes = local.len;
    status.server_state = server.state; status.client_state = client.phase; *out = status;
}
int ble_ras_set_offset(int32_t mm)
{
    if (mm < -10000 || mm > 10000) return BLE_HS_EINVAL;
    status.offset_mm = mm;
    status.distance.status = RAS_INCOMPLETE; /* Requires a new measurement. */
    return 0;
}
int ble_ras_run_selftest(struct cs_distance_result *out)
{
    if (server.conn != BLE_HS_CONN_HANDLE_NONE) return BLE_HS_EBUSY;
    int rc = ras_selftest(&local, &remote, &work, out);
    ras_record_reset(&local); memset(&remote, 0, sizeof remote);
    return rc;
}
