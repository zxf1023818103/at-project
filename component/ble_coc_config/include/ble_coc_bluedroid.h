/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
/* Standalone advertiser/service. Call once after the platform has enabled
 * Bluedroid. The backend owns GAP callbacks and its BTU-posting worker. */
void ble_coc_bluedroid_init(void);

/* Optional application observer, e.g. pairing policy and diagnostics. The
 * benchmark keeps ownership of GAP registration. Default implementation is empty. */
#include "esp_gap_ble_api.h"
void ble_coc_bluedroid_gap_event(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
