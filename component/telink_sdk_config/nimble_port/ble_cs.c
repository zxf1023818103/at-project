/* SPDX-License-Identifier: Apache-2.0
 * Standard HCI CS host adaptation for NimBLE 1.10.0. Replaces its experimental
 * ble_cs.c, while retaining upstream HCI dispatch, event masks and transport.
 * No vendor LL API is called from the host. One config (0), one ACL connection.
 */
#include <string.h>
#include "host/ble_hs.h"
#include "host/ble_cs.h"
#include "ble_hs_hci_priv.h"
#include "ble_cs_priv.h"

_Static_assert(sizeof(struct ble_hci_le_cs_rd_loc_supp_cap_rp) == 28, "Core 6.0 local CS capabilities");
_Static_assert(sizeof(struct ble_hci_le_cs_wr_cached_rem_supp_cap_cp) == 30, "Core 6.0 cached CS capabilities");
_Static_assert(sizeof(struct ble_hci_ev_le_subev_cs_rd_rem_supp_cap_complete) == 32, "Core 6.0 remote CS capabilities");
_Static_assert(sizeof(struct ble_hci_ev_le_subev_cs_proc_enable_complete) == 22, "Core 6.0 CS procedure event");

enum { IDLE, CAPABILITIES, FAE, CONFIG, SECURITY, ENABLE, RUNNING, STOPPING };
static struct {
    uint16_t handle, procedure_counter, start_acl_event;
    int16_t frequency_compensation;
    int8_t reference_power;
    uint8_t phase, configured, security, continuation, result_config, config_id, role;
    ble_cs_event_fn *cb;
    void *arg;
} cs = {.handle = BLE_HS_CONN_HANDLE_NONE};

static uint16_t get16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static void put16(uint8_t *p, unsigned n) { p[0] = n; p[1] = n >> 8; }
static void put24(uint8_t *p, unsigned n) { put16(p, n); p[2] = n >> 16; }
static int command(uint8_t ocf, const void *cp, uint8_t n, void *rp, uint8_t rn)
{
    return ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE, ocf), cp, n, rp, rn);
}
static int connected(uint16_t handle, int encrypted)
{
    struct ble_gap_conn_desc desc;
    int rc = ble_gap_conn_find(handle, &desc);
    if (rc) return rc;
    return encrypted && !desc.sec_state.encrypted ? BLE_HS_EAUTHEN : 0;
}
static void emit(struct ble_cs_event *ev) { if (cs.cb) cs.cb(ev, cs.arg); }
static int fail(int rc)
{
    cs.phase = IDLE;
    cs.continuation = 0;
    struct ble_cs_event ev = {.type = BLE_CS_EVENT_ERROR};
    ev.error.conn_handle = cs.handle;
    ev.error.status = rc;
    emit(&ev);
    return rc;
}
void ble_cs_reset(uint16_t handle)
{
    if (handle == BLE_HS_CONN_HANDLE_NONE || handle == cs.handle) {
        ble_cs_event_fn *cb = cs.cb;
        void *arg = cs.arg;
        memset(&cs, 0, sizeof cs);
        cs.handle = BLE_HS_CONN_HANDLE_NONE;
        cs.cb = cb; cs.arg = arg;
    }
}
int ble_cs_enable_host_support(void)
{
    /* Core 6.0 Vol 6 Part B: Channel Sounding = bit 46, Host Support = bit 47.
     * Set before creating any connection; the peer caches exchanged features. */
    const uint8_t cp[] = {47, 1};
    uint8_t features[8];
    int rc = command(0x74, cp, sizeof cp, NULL, 0);
    if (rc) return rc;
    rc = command(0x03, NULL, 0, features, sizeof features);
    if (rc) return rc;
    return (features[5] & 0xc0) == 0xc0 ? 0 : BLE_HS_ENOTSUP;
}
int ble_cs_read_local_capabilities(struct ble_hci_le_cs_rd_loc_supp_cap_rp *caps)
{
    if (!caps) return BLE_HS_EINVAL;
    memset(caps, 0, sizeof *caps);
    return command(0x89, NULL, 0, caps, sizeof *caps);
}
int ble_cs_reflector_setup(struct ble_cs_reflector_setup_params *p)
{
    if (!p) return BLE_HS_EINVAL;
    cs.cb = p->cb; cs.arg = p->cb_arg;
    return 0;
}
int ble_cs_set_default_settings(uint16_t handle, uint8_t roles)
{
    int rc = connected(handle, 0);
    if (rc) return rc;
    if (roles > 3) return BLE_HS_EINVAL;
    uint8_t cp[5], rp[2];
    put16(cp, handle); cp[2] = roles; cp[3] = 1; cp[4] = 0; /* Single antenna, 0 dBm. */
    rc = command(0x8d, cp, sizeof cp, rp, sizeof rp);
    if (!rc && get16(rp) != handle) return BLE_HS_ECONTROLLER;
    if (!rc) cs.handle = handle;
    return rc;
}
static int handle_command(uint8_t ocf, uint16_t handle, int encrypted)
{
    int rc = connected(handle, encrypted);
    if (rc) return rc;
    uint8_t cp[2]; put16(cp, handle);
    return command(ocf, cp, sizeof cp, NULL, 0);
}
int ble_cs_read_remote_capabilities(uint16_t handle) { return handle_command(0x8a, handle, 0); }
int ble_cs_security_enable(uint16_t handle) { return handle_command(0x8c, handle, 1); }
int ble_cs_remove_config(uint16_t handle, uint8_t id)
{
    int rc = connected(handle, 0);
    if (rc) return rc;
    if (id > 3) return BLE_HS_EINVAL;
    uint8_t cp[3]; put16(cp, handle); cp[2] = id;
    return command(0x91, cp, sizeof cp, NULL, 0);
}
static int create_config(void)
{
    struct ble_hci_le_cs_create_config_cp cp = {0};
    put16((void *)&cp.conn_handle, cs.handle);
    cp.create_context = 1;
    cp.main_mode_type = 2; /* PBR; TL323X does not support RTT mode 1/3. */
    cp.sub_mode_type = 0xff;
    cp.min_main_mode_steps = cp.max_main_mode_steps = 0xff;
    cp.mode_0_steps = 3;
    cp.cs_sync_phy = 1;
    const uint8_t map[10] = {0xfc,0xff,0x7f,0xfc,0xff,0xff,0xff,0xff,0xff,0x1f};
    memcpy(cp.channel_map, map, sizeof map);
    cp.channel_map_repetition = 1;
    cp.ch3c_jump = 2;
    cs.phase = CONFIG;
    int rc = command(0x90, &cp, sizeof cp, NULL, 0);
    return rc ? fail(rc) : 0;
}
static int enable_procedure(void)
{
    struct ble_hci_le_cs_set_proc_params_cp cp = {0};
    uint8_t rp[2];
    put16((void *)&cp.conn_handle, cs.handle);
    cp.config_id = cs.config_id;
    put16((void *)&cp.max_procedure_len, 0xffff);
    put16((void *)&cp.min_procedure_interval, 10);
    put16((void *)&cp.max_procedure_interval, 10);
    put16((void *)&cp.max_procedure_count, 1); /* One bounded measurement per start. */
    put24(cp.min_subevent_len, 60000);
    put24(cp.max_subevent_len, 60000);
    cp.phy = 1;
    cp.preferred_peer_antenna = 1;
    cp.snr_control_initiator = cp.snr_control_reflector = 0xff;
    int rc = command(0x93, &cp, sizeof cp, rp, sizeof rp);
    if (rc) return fail(rc);
    if (get16(rp) != cs.handle) return fail(BLE_HS_ECONTROLLER);
    uint8_t enable[4]; put16(enable, cs.handle); enable[2] = cs.config_id; enable[3] = 1;
    cs.phase = ENABLE;
    rc = command(0x94, enable, sizeof enable, NULL, 0);
    return rc ? fail(rc) : 0;
}
int ble_cs_initiator_procedure_start(const struct ble_cs_initiator_procedure_start_params *p)
{
    if (!p) return BLE_HS_EINVAL;
    int rc = connected(p->conn_handle, 1);
    if (rc) return rc;
    if (cs.phase != IDLE) return BLE_HS_EBUSY;
    /* Reusing a peer-created Reflector config cannot change its CS role.
     * Require removal first instead of enabling it as an Initiator silently. */
    if (cs.configured && cs.role != 0) return BLE_HS_EALREADY;
    cs.cb = p->cb; cs.arg = p->cb_arg;
    rc = ble_cs_set_default_settings(p->conn_handle, 3);
    if (rc) return rc;
    cs.continuation = 0;
    if (cs.configured && cs.security) return enable_procedure();
    cs.phase = CAPABILITIES;
    rc = ble_cs_read_remote_capabilities(p->conn_handle);
    return rc ? fail(rc) : 0;
}
int ble_cs_initiator_procedure_terminate(uint16_t handle)
{
    int rc = connected(handle, 0);
    if (rc) return rc;
    if (handle != cs.handle || !cs.configured) return BLE_HS_EALREADY;
    uint8_t cp[4]; put16(cp, handle); cp[2] = cs.config_id; cp[3] = 0;
    rc = command(0x94, cp, sizeof cp, NULL, 0);
    if (!rc) cs.phase = STOPPING;
    return rc;
}
/* Status events have a fixed header. Error events may contain only that header. */
static int status_event(uint8_t code, const void *data, unsigned n, unsigned success_len)
{
    const uint8_t *p = data;
    if (n < 4 || p[0] != code) return BLE_HS_EBADDATA;
    if (get16(p + 2) != cs.handle) return BLE_HS_ENOTCONN;
    if (p[1]) return fail(BLE_HS_HCI_ERR(p[1]));
    if (n != success_len) return fail(BLE_HS_EBADDATA);
    struct ble_cs_event ev = {.type = BLE_CS_EVENT_HCI};
    ev.hci.conn_handle = cs.handle; ev.hci.subevent = code; ev.hci.data = data; ev.hci.len = n;
    emit(&ev);
    return 0;
}
int ble_hs_hci_evt_le_cs_rd_rem_supp_cap_complete(uint8_t code, const void *data, unsigned n)
{
    int rc = status_event(code, data, n, sizeof(struct ble_hci_ev_le_subev_cs_rd_rem_supp_cap_complete));
    if (rc || cs.phase != CAPABILITIES) return rc;
    const struct ble_hci_ev_le_subev_cs_rd_rem_supp_cap_complete *p = data;
    if (!(p->roles_supported & 2)) return fail(BLE_HS_ENOTSUP);
    /* Bit 1: zero FAE in Reflector role; no table exchange is necessary. */
    if (get16((const void *)&p->optional_subfeatures_supported) & 2) return create_config();
    cs.phase = FAE;
    rc = handle_command(0x8e, cs.handle, 0);
    return rc ? fail(rc) : 0;
}
int ble_hs_hci_evt_le_cs_rd_rem_fae_complete(uint8_t code, const void *data, unsigned n)
{
    int rc = status_event(code, data, n, sizeof(struct ble_hci_ev_le_subev_cs_rd_rem_fae_complete));
    return rc || cs.phase != FAE ? rc : create_config();
}
int ble_hs_hci_evt_le_cs_config_complete(uint8_t code, const void *data, unsigned n)
{
    int rc = status_event(code, data, n, sizeof(struct ble_hci_ev_le_subev_cs_config_complete));
    if (rc) return rc;
    const struct ble_hci_ev_le_subev_cs_config_complete *p = data;
    cs.config_id = p->config_id;
    cs.role = p->role;
    cs.configured = p->action == 1;
    if (!cs.configured) { cs.phase = IDLE; cs.continuation = 0; return 0; }
    if (p->main_mode_type != 2 || p->sub_mode_type != 0xff) {
        /* Buffers follow Telink's TL323X PBR-only sample. Decline RTT configs. */
        (void)ble_cs_remove_config(cs.handle, cs.config_id);
        return fail(BLE_HS_ENOTSUP);
    }
    /* Reflector accepts peer's configuration; never launch initiator commands. */
    if (cs.phase != CONFIG) return 0;
    if (cs.security) return enable_procedure();
    struct ble_gap_conn_desc desc;
    rc = ble_gap_conn_find(cs.handle, &desc);
    if (rc) return fail(rc);
    cs.phase = SECURITY;
    /* Only ACL Central may start CS Security; a Peripheral waits for its peer. */
    if (desc.role != BLE_GAP_ROLE_MASTER) return 0;
    rc = ble_cs_security_enable(cs.handle);
    return rc ? fail(rc) : 0;
}
int ble_hs_hci_evt_le_cs_sec_enable_complete(uint8_t code, const void *data, unsigned n)
{
    int rc = status_event(code, data, n, sizeof(struct ble_hci_ev_le_subev_cs_sec_enable_complete));
    if (rc) return rc;
    cs.security = 1;
    return cs.phase == SECURITY ? enable_procedure() : 0;
}
int ble_hs_hci_evt_le_cs_proc_enable_complete(uint8_t code, const void *data, unsigned n)
{
    int rc = status_event(code, data, n, sizeof(struct ble_hci_ev_le_subev_cs_proc_enable_complete));
    if (rc) return rc;
    const struct ble_hci_ev_le_subev_cs_proc_enable_complete *p = data;
    cs.phase = p->state ? RUNNING : IDLE;
    if (!p->state) cs.continuation = 0;
    return 0;
}
int ble_cs_step_data_validate(const uint8_t *p, unsigned n, unsigned count)
{
    if (!p && n) return BLE_HS_EBADDATA;
    unsigned at = 0;
    for (unsigned i = 0; i < count; i++) {
        if (n - at < 3 || p[at] > 3 || p[at + 1] > 78 || p[at + 2] > n - at - 3)
            return BLE_HS_EBADDATA;
        at += 3 + p[at + 2];
    }
    return at == n ? 0 : BLE_HS_EBADDATA;
}
static int result_event(const uint8_t *p, unsigned n, int continuation)
{
    unsigned header = continuation ? 9 : 16;
    if (n < header) return fail(BLE_HS_EBADDATA);
    if (get16(p + 1) != cs.handle) return BLE_HS_ENOTCONN;
    if (continuation && (!cs.continuation || cs.result_config != p[3])) return fail(BLE_HS_EBADDATA);
    if (!continuation && cs.continuation) return fail(BLE_HS_EBADDATA);
    int rc = ble_cs_step_data_validate(p + header, n - header, p[header - 1]);
    if (rc) return fail(rc);
    if ((p[header - 5] != 0 && p[header - 5] != 1 && p[header - 5] != 15) ||
        (p[header - 4] != 0 && p[header - 4] != 1 && p[header - 4] != 15) ||
        p[header - 2] > 4) return fail(BLE_HS_EBADDATA);
    if (!continuation) {
        cs.start_acl_event = get16(p + 4);
        cs.procedure_counter = get16(p + 6);
        cs.frequency_compensation = get16(p + 8);
        cs.reference_power = p[10];
        cs.result_config = p[3];
    }
    struct ble_cs_event ev = {.type = BLE_CS_EVENT_SUBEVENT_RESULT};
    ev.result.conn_handle = cs.handle;
    ev.result.config_id = p[3];
    ev.result.procedure_counter = cs.procedure_counter;
    ev.result.start_acl_event = cs.start_acl_event;
    ev.result.frequency_compensation = cs.frequency_compensation;
    ev.result.reference_power = cs.reference_power;
    ev.result.continuation = continuation;
    ev.result.procedure_status = p[header - 5];
    ev.result.subevent_status = p[header - 4];
    ev.result.abort_reason = p[header - 3];
    ev.result.num_antenna_paths = p[header - 2];
    ev.result.num_steps = p[header - 1];
    ev.result.steps = p + header; ev.result.steps_len = n - header;
    cs.continuation = ev.result.subevent_status == 1;
    emit(&ev);
    if (ev.result.procedure_status != 1 && !cs.continuation) {
        cs.phase = IDLE;
        struct ble_cs_event done = {.type = BLE_CS_EVENT_CS_PROCEDURE_COMPLETE};
        done.procedure_complete.conn_handle = cs.handle;
        done.procedure_complete.status = ev.result.procedure_status ? BLE_HS_EAPP : 0;
        emit(&done);
    }
    return 0;
}
int ble_hs_hci_evt_le_cs_subevent_result(uint8_t code, const void *data, unsigned n)
{
    (void)code; return result_event(data, n, 0);
}
int ble_hs_hci_evt_le_cs_subevent_result_continue(uint8_t code, const void *data, unsigned n)
{
    (void)code; return result_event(data, n, 1);
}
int ble_hs_hci_evt_le_cs_test_end_complete(uint8_t code, const void *data, unsigned n)
{
    const uint8_t *p = data;
    (void)code;
    return n == 2 ? (p[1] ? fail(BLE_HS_HCI_ERR(p[1])) : 0) : BLE_HS_EBADDATA;
}
