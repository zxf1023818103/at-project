/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#include <stddef.h>

/* Bounded to the demo's single antenna, PBR Mode 2 configuration. */
#define RAS_MAX_STEPS 256
#define RAS_MAX_SUBEVENTS 32
#define RAS_MAX_BODY (4 + RAS_MAX_SUBEVENTS * 8 + RAS_MAX_STEPS * 10)
enum ras_error {
    RAS_OK, RAS_INVALID, RAS_OVERFLOW, RAS_SEQUENCE, RAS_INCOMPLETE,
    RAS_MISMATCH, RAS_QUALITY, RAS_UNSUPPORTED
};
struct ras_fragment {
    uint16_t counter, start_acl_event;
    int16_t frequency_compensation;
    int8_t reference_power;
    uint8_t config, continuation, procedure_status, subevent_status, abort_reason;
    uint8_t paths, num_steps;
    const uint8_t *steps;
    unsigned steps_len;
};
struct ras_record {
    uint8_t body[RAS_MAX_BODY], channels[RAS_MAX_STEPS];
    uint16_t len, counter, step_count, sub_offset;
    uint8_t role, active, in_subevent, complete, failed, subevents, sub_steps;
};
struct ras_rx {
    uint8_t body[RAS_MAX_BODY];
    uint16_t len;
    uint8_t started, complete, next_seq;
};
static inline uint16_t ras_get16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static inline void ras_put16(uint8_t *p, unsigned v) { p[0] = v; p[1] = v >> 8; }
void ras_record_reset(struct ras_record *record);
int ras_record_append(struct ras_record *record, const struct ras_fragment *fragment,
                      uint8_t role, int8_t tx_power);
/* Full, unfiltered IQ format: Mode 0 is role dependent; Mode 2 includes extension slot. */
int ras_step_size(uint8_t mode, uint8_t role);
int ras_rx_segment(struct ras_rx *rx, const uint8_t *segment, unsigned len);
/* Returns ATT value size, or zero for invalid parameters. Does not mutate offset. */
unsigned ras_segment(const uint8_t *body, unsigned len, unsigned offset, uint8_t seq,
                     unsigned mtu, uint8_t *out, unsigned capacity);
