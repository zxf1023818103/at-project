/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "host/ble_hs.h"
#include "host/ble_cs.h"
#include "ble_cs_priv.h"
#include "cs_hci.h"

static unsigned commands, opcode, cmd_len, encrypted = 1, role;
static int inject_error;
static uint8_t cs_features = 0xc0;
static uint8_t sent[255], collected[2048];
static unsigned collected_len, fragments, completions, errors, packet_count;
int ble_gap_conn_find(uint16_t h, struct ble_gap_conn_desc *d)
{
    if (h != 1) return BLE_HS_ENOTCONN;
    memset(d, 0, sizeof *d); d->role = role; d->sec_state.encrypted = encrypted;
    return 0;
}
int ble_hs_hci_cmd_tx(uint16_t op, const void *p, uint8_t n, void *r, uint8_t rn)
{
    commands++; opcode = op; cmd_len = n;
    if (n) memcpy(sent, p, n);
    if (inject_error) return inject_error;
    if (op == 0x2074) assert(n == 2 && sent[0] == 47 && sent[1] == 1);
    if (rn) { memset(r, 0, rn); if (rn == 2) memcpy(r, p, 2); }
    if (op == 0x2003) { assert(rn == 8); ((uint8_t *)r)[5] = cs_features; }
    if (op == 0x2089) { assert(rn == 28); ((uint8_t *)r)[27] = 0x1f; }
    return 0;
}
static int callback(struct ble_cs_event *e, void *arg)
{
    (void)arg;
    if (e->type == BLE_CS_EVENT_SUBEVENT_RESULT) {
        assert(e->result.conn_handle == 1);
        assert(e->result.procedure_counter == 37);
        assert(e->result.steps_len + collected_len <= sizeof collected);
        memcpy(collected + collected_len, e->result.steps, e->result.steps_len);
        collected_len += e->result.steps_len; fragments++;
    } else if (e->type == BLE_CS_EVENT_CS_PROCEDURE_COMPLETE) completions++;
    else if (e->type == BLE_CS_EVENT_ERROR) errors++;
    return 0;
}
static void bind_reflector(void)
{
    ble_cs_reset(BLE_HS_CONN_HANDLE_NONE);
    struct ble_cs_reflector_setup_params p = {callback, NULL};
    assert(ble_cs_reflector_setup(&p) == 0);
    assert(ble_cs_set_default_settings(1, 2) == 0);
    commands = 0;
}
static void receive(const uint8_t *p, unsigned n, void *arg)
{
    (void)arg; packet_count++;
    assert(n <= 255);
    int rc = p[0] == 0x31 ? ble_hs_hci_evt_le_cs_subevent_result(p[0], p, n) :
        ble_hs_hci_evt_le_cs_subevent_result_continue(p[0], p, n);
    assert(rc == 0);
}
int main(void)
{
    assert(!ble_cs_enable_host_support());
    assert(commands == 2 && opcode == 0x2003);
    cs_features = 0x40; /* Controller must report Host Support, not just CS. */
    assert(ble_cs_enable_host_support() == BLE_HS_ENOTSUP);
    cs_features = 0x80;
    assert(ble_cs_enable_host_support() == BLE_HS_ENOTSUP);
    cs_features = 0xc0;
    inject_error = BLE_HS_HCI_ERR(0x0c);
    unsigned rejected = commands;
    assert(ble_cs_enable_host_support() == inject_error && commands == rejected + 1);
    inject_error = 0;
    struct ble_hci_le_cs_rd_loc_supp_cap_rp local_caps;
    assert(!ble_cs_read_local_capabilities(&local_caps));
    assert(local_caps.optional_tx_snr_capability == 0x1f);
    commands = 0;
    struct ble_cs_initiator_procedure_start_params start = {1, callback, NULL};
    assert(ble_cs_initiator_procedure_start(NULL) == BLE_HS_EINVAL);
    encrypted = 0;
    assert(ble_cs_initiator_procedure_start(&start) == BLE_HS_EAUTHEN);
    assert(commands == 0);
    encrypted = 1;
    bind_reflector();
    /* Reflector events must not trigger initiator-only configuration commands. */
    struct ble_hci_ev_le_subev_cs_config_complete config = {
        .subev_code=0x2f, .conn_handle=1, .action=1, .main_mode_type=2, .sub_mode_type=0xff, .role=1};
    assert(!ble_hs_hci_evt_le_cs_config_complete(0x2f, &config, sizeof config));
    assert(commands == 0);
    assert(ble_cs_initiator_procedure_start(&start) == BLE_HS_EALREADY);
    assert(!ble_cs_initiator_procedure_terminate(1));
    assert(opcode == 0x2094 && cmd_len == 4 && sent[3] == 0);
    bind_reflector();
    config.role = 0;
    assert(!ble_cs_initiator_procedure_start(&start));
    assert(opcode == 0x208a);
    assert(ble_cs_initiator_procedure_start(&start) == BLE_HS_EBUSY);
    struct ble_hci_ev_le_subev_cs_rd_rem_supp_cap_complete cap = {
        .subev_code=0x2c, .conn_handle=1, .roles_supported=2, .optional_subfeatures_supported=2};
    assert(!ble_hs_hci_evt_le_cs_rd_rem_supp_cap_complete(0x2c, &cap, sizeof cap));
    assert(opcode == 0x2090 && sent[4] == 2 && sent[5] == 0xff); /* PBR, no RTT. */
    assert(!ble_hs_hci_evt_le_cs_config_complete(0x2f, &config, sizeof config));
    assert(opcode == 0x208c);
    uint8_t security[] = {0x2e,0,1,0};
    assert(!ble_hs_hci_evt_le_cs_sec_enable_complete(0x2e, security, sizeof security));
    assert(opcode == 0x2094 && sent[3] == 1);
    struct ble_hci_ev_le_subev_cs_proc_enable_complete enabled = {
        .subev_code=0x30, .conn_handle=1, .state=1, .max_procedure_len=0xffff};
    assert(!ble_hs_hci_evt_le_cs_proc_enable_complete(0x30, &enabled, sizeof enabled));
    assert(ble_hs_hci_evt_le_cs_proc_enable_complete(0x30, &enabled, 20) == BLE_HS_EBADDATA);
    /* A nonzero-FAE Reflector needs its table before configuration. */
    bind_reflector();
    assert(!ble_cs_initiator_procedure_start(&start));
    cap.optional_subfeatures_supported = 0;
    assert(!ble_hs_hci_evt_le_cs_rd_rem_supp_cap_complete(0x2c, &cap, sizeof cap));
    assert(opcode == 0x208e);
    struct ble_hci_ev_le_subev_cs_rd_rem_fae_complete fae = {.subev_code=0x2d, .conn_handle=1};
    assert(!ble_hs_hci_evt_le_cs_rd_rem_fae_complete(0x2d, &fae, sizeof fae));
    assert(opcode == 0x2090);
    /* Pairing/security errors stop the state machine before procedure enable. */
    bind_reflector();
    assert(!ble_cs_initiator_procedure_start(&start));
    uint8_t failed[] = {0x2c,0x1a,1,0};
    unsigned before = commands;
    assert(ble_hs_hci_evt_le_cs_rd_rem_supp_cap_complete(0x2c, failed, sizeof failed) == BLE_HS_HCI_ERR(0x1a));
    assert(commands == before && errors);
    bind_reflector();
    /* Whole 75-step LL report -> multiple standard HCI events -> host callback. */
    uint8_t report[16 + 75 * 12] = {0x31,1,0,0,9,0,37,0};
    report[14] = 1; report[15] = 75;
    for (unsigned i = 0; i < 75; i++) {
        uint8_t *p = report + 16 + i * 12;
        p[0] = 2; p[1] = 2 + i % 70; p[2] = 9;
        for (unsigned j = 0; j < 9; j++) p[j+3] = i + j;
    }
    assert(tl3238_cs_hci_split(report, sizeof report, 1, receive, NULL) == -1);
    assert(packet_count == 0); /* Backpressure must be all-or-nothing. */
    assert(tl3238_cs_hci_split(report, sizeof report, 8, receive, NULL) == 4);
    assert(packet_count == 4 && fragments == 4 && completions == 1);
    assert(collected_len == 900 && !memcmp(collected, report + 16, 900));
    assert(tl3238_cs_hci_split(report, sizeof report - 1, 8, receive, NULL) == -1);
    uint8_t orphan[] = {0x32,1,0,0,0,0,0,1,0};
    assert(ble_hs_hci_evt_le_cs_subevent_result_continue(0x32, orphan, sizeof orphan) == BLE_HS_EBADDATA);
    ble_cs_reset(1);
    assert(ble_hs_hci_evt_le_cs_subevent_result(0x31, report, sizeof report) == BLE_HS_ENOTCONN);
    /* Fuzz actual parsers under ASan/UBSan: truncation, bad lengths and counts. */
    uint8_t fuzz[260]; unsigned rng = 0x3238;
    for (unsigned i = 0; i < 100000; i++) {
        for (unsigned j = 0; j < sizeof fuzz; j++) { rng = rng * 1664525 + 1013904223; fuzz[j] = rng >> 24; }
        unsigned n = rng % sizeof fuzz;
        (void)ble_cs_step_data_validate(fuzz, n, (rng >> 8) & 255);
        fuzz[0] = i & 1 ? 0x31 : 0x32;
        (void)tl3238_cs_hci_split(fuzz, n, 0, receive, NULL);
    }
    puts("PASS: CS Host Support/readback, Core 6.0 wire fields, roles, security failures, start/stop, 75-step fragmentation, backpressure, disconnect, 100000 malformed packets (ASan/UBSan)");
    return 0;
}
