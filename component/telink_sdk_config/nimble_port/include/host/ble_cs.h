/* SPDX-License-Identifier: Apache-2.0
 * Completes the experimental NimBLE CS API for the single-connection demo.
 * Call control APIs from the NimBLE host event queue. Callback data is borrowed
 * until callback return. All HCI structures contain little-endian wire fields.
 */
#pragma once
#include <stdint.h>
#include "nimble/hci_common.h"

#define BLE_CS_EVENT_CS_PROCEDURE_COMPLETE 0
#define BLE_CS_EVENT_HCI 1
#define BLE_CS_EVENT_SUBEVENT_RESULT 2
#define BLE_CS_EVENT_ERROR 3

struct ble_cs_event {
    uint8_t type;
    union {
        struct { uint16_t conn_handle; int status; } procedure_complete;
        struct { uint16_t conn_handle; uint8_t subevent; const uint8_t *data; unsigned len; } hci;
        struct {
            uint16_t conn_handle, procedure_counter, start_acl_event;
            int16_t frequency_compensation;
            int8_t reference_power;
            uint8_t config_id, continuation, procedure_status, subevent_status;
            uint8_t abort_reason, num_antenna_paths, num_steps;
            const uint8_t *steps;
            unsigned steps_len;
        } result;
        struct { uint16_t conn_handle; int status; } error;
    };
};
typedef int ble_cs_event_fn(struct ble_cs_event *event, void *arg);
struct ble_cs_initiator_procedure_start_params {
    uint16_t conn_handle;
    ble_cs_event_fn *cb;
    void *cb_arg;
};
struct ble_cs_reflector_setup_params { ble_cs_event_fn *cb; void *cb_arg; };
int ble_cs_initiator_procedure_start(const struct ble_cs_initiator_procedure_start_params *params);
int ble_cs_initiator_procedure_terminate(uint16_t conn_handle);
int ble_cs_reflector_setup(struct ble_cs_reflector_setup_params *params);
/* Call once after host sync, before advertising/connecting. Verifies bits 46/47. */
int ble_cs_enable_host_support(void);
int ble_cs_read_local_capabilities(struct ble_hci_le_cs_rd_loc_supp_cap_rp *caps);
int ble_cs_set_default_settings(uint16_t conn_handle, uint8_t roles);
int ble_cs_read_remote_capabilities(uint16_t conn_handle);
int ble_cs_security_enable(uint16_t conn_handle);
int ble_cs_remove_config(uint16_t conn_handle, uint8_t config_id);
void ble_cs_reset(uint16_t conn_handle);
int ble_cs_step_data_validate(const uint8_t *data, unsigned len, unsigned count);
