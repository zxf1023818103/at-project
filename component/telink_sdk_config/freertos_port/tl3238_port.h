/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>

/* Keep every one-shot interval below the signed half-range of the 24 MHz,
 * 32-bit STIMER. Longer/indefinite idle periods are split into bounded sleeps. */
#define TL3238_MAX_IDLE_MS 60000u

typedef struct {
    uint32_t enabled;
    uint32_t tick_irqs;
    uint32_t sleep_entries;
    uint32_t skipped_ticks;
    uint32_t timer_wakes;
    uint32_t uart_wakes;
    uint32_t other_wakes;
    uint32_t aborted_sleeps;
    uint32_t last_idle_ticks;
    uint32_t max_idle_ticks;
} Tl3238TicklessStats;

/* Task-context diagnostic controls; disabling restores periodic ticks. */
void tl3238_tickless_enable(uint32_t enabled);
void tl3238_tickless_get_stats(Tl3238TicklessStats *stats);
