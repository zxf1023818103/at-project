/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "host/ble_gap.h"
#include "host/ble_uuid.h"

typedef void (*ble_coc_log_fn)(const char *format, ...);
extern const ble_uuid128_t ble_coc_nimble_uuid;

/* Once, after nimble_port_init and before starting the host event queue.
 * log may be NULL. The singleton and RX pool live for the host's lifetime.
 * No controller, task, CLI or advertising is created by this adapter. */
int ble_coc_nimble_init(ble_coc_log_fn log);
/* Forward GAP events and the host reset callback from the host task. */
void ble_coc_nimble_gap_event(struct ble_gap_event *event);
void ble_coc_nimble_reset(void);
/* Diagnostic calls use the NPL critical section for a snapshot. The firmware
 * must be single-core, or its NPL critical section must serialize host access. */
void ble_coc_nimble_status(char *output, size_t capacity);
bool ble_coc_nimble_is_open(void);
