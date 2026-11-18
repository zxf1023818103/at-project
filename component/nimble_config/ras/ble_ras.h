/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "cs_distance.h"
struct ble_gap_event;
struct ble_cs_event;
struct ble_ras_status {
    uint16_t conn, local_counter, remote_counter, bytes;
    uint8_t service, client_ready, discovering, client_state, server_state, available;
    unsigned tx_segments, rx_segments, overwritten, errors;
    int last_rc;
    int32_t offset_mm;
    struct cs_distance_result distance;
};
/* Single connection. All APIs run on the NimBLE host event queue. */
int ble_ras_init(void);
void ble_ras_reset(void);
void ble_ras_gap_event(struct ble_gap_event *event);
void ble_ras_cs_event(const struct ble_cs_event *event);
int ble_ras_discover(void);
int ble_ras_get(void);
int ble_ras_set_offset(int32_t offset_mm);
void ble_ras_status(struct ble_ras_status *out);
int ble_ras_run_selftest(struct cs_distance_result *out);
