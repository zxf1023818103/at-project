/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>

typedef struct {
    uint32_t produced, consumed, sequence_errors, timer_callbacks, fpu_errors;
} DemoStats;
extern volatile DemoStats demo_stats;
extern volatile uint32_t console_rx_dropped, console_rx_errors, console_tx_errors;
void console_init(void);
void console_write(const char *text);
void console_task(void *arg);
BaseType_t console_rx_isr(void);
BaseType_t console_rx_pending(void);
void cli_register_commands(void);
TickType_t demo_idle_window(TickType_t duration, uint32_t *hardware_ms);
