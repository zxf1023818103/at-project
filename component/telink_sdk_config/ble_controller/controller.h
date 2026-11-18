/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct {
    uint32_t ready, init_error, loops, irqs, commands, tx_acl, events, rx_acl, errors;
    uint32_t cs_ready, cs_calibration, cs_irqs, cs_events, cs_drops;
} Tl3238BleControllerStats;
extern volatile Tl3238BleControllerStats tl3238_controller_stats;
void tl3238_ble_board_init(void);
void tl3238_controller_start(void);
void tl3238_controller_irq(void);
void tl3238_controller_wake_from_isr(void);
int tl3238_controller_send(uint8_t type, const void *data, size_t len);
int tl3238_controller_can_send(void);
#ifdef TL3238_ENABLE_CS
/* V4048: at most 75 steps per subevent, at most 17 raw HCI bytes per step.
 * Checked against the SDK's actual allocation in channel_sounding.c. */
#define TL3238_CS_RAW_SUBEVENT_CAPACITY (75u * 17u)
void tl3238_controller_cs_init(void);
void tl3238_controller_cs_irq(void);
void tl3238_controller_cs_poll(void);
int tl3238_controller_cs_command(uint8_t *h4);
int tl3238_controller_event(unsigned int h, uint8_t *p, int n);
#endif
/* Implemented by the selected host; keep vendor and host headers separate. */
int tl3238_host_receive(const uint8_t *h4, size_t len);
