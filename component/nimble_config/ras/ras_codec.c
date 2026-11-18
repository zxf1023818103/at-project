/* SPDX-License-Identifier: Apache-2.0
 * RAS 1.0 sections 3.2.1/3.2.2 + Erratum 26610 (2025-08-11).
 * Explicit byte encoding avoids ABI bitfields.
 */
#include "ras_codec.h"
#include <string.h>

int ras_step_size(uint8_t mode, uint8_t role)
{
    if (mode & 0x80) return 0;
    if (mode == 0) return role == 0 ? 5 : 3;
    if (mode == 2) return 9; /* permutation + (one antenna path + extension) * 4 */
    return -1;
}
void ras_record_reset(struct ras_record *r) { memset(r, 0, sizeof *r); }
static int bad(struct ras_record *r, int rc) { r->failed = 1; r->complete = 0; return rc; }
int ras_record_append(struct ras_record *r, const struct ras_fragment *f, uint8_t role, int8_t tx)
{
    if (!r || !f || role > 1 || f->config > 3 || f->paths != 1 ||
        (!f->steps && f->steps_len)) return RAS_INVALID;
    if ((f->procedure_status != 0 && f->procedure_status != 1 && f->procedure_status != 15) ||
        (f->subevent_status != 0 && f->subevent_status != 1 && f->subevent_status != 15))
        return bad(r, RAS_INVALID);
    if (!f->continuation && (!r->active || f->counter != r->counter)) {
        ras_record_reset(r);
        r->active = 1; r->counter = f->counter; r->role = role; r->len = 4;
        ras_put16(r->body, (f->counter & 0xfff) | (unsigned)f->config << 12);
        r->body[2] = (uint8_t)tx;
        /* Erratum 26610, Table 3.7: low nibble Previously Used, high nibble RFU.
         * Core Vol 1, Part E, 2.4.1/2.4.2 requires zero on transmission. */
        r->body[3] = 0;
    }
    if (!r->active || r->failed || r->complete || r->counter != f->counter ||
        role != r->role || (r->body[1] >> 4) != f->config ||
        !!f->continuation != !!r->in_subevent) return bad(r, RAS_SEQUENCE);
    if (!f->continuation) {
        if (r->subevents == RAS_MAX_SUBEVENTS || r->len + 8u > sizeof r->body)
            return bad(r, RAS_OVERFLOW);
        r->sub_offset = r->len; r->len += 8;
        uint8_t *s = r->body + r->sub_offset;
        ras_put16(s, f->start_acl_event); ras_put16(s + 2, (uint16_t)f->frequency_compensation);
        s[6] = (uint8_t)f->reference_power;
        r->sub_steps = 0; r->subevents++; r->in_subevent = 1;
    }
    unsigned at = 0;
    for (unsigned i = 0; i < f->num_steps; i++) {
        if (f->steps_len - at < 3) return bad(r, RAS_INVALID);
        uint8_t mode = f->steps[at], ch = f->steps[at + 1], n = f->steps[at + 2];
        if (mode > 3 || ch > 78 || n > f->steps_len - at - 3) return bad(r, RAS_INVALID);
        if (n && ras_step_size(mode, role) != n) return bad(r, RAS_UNSUPPORTED);
        if (r->step_count == RAS_MAX_STEPS || r->sub_steps == 160 ||
            r->len + 1u + n > sizeof r->body) return bad(r, RAS_OVERFLOW);
        r->channels[r->step_count++] = ch; r->sub_steps++;
        r->body[r->len++] = n ? mode : 0x80;
        if (n) memcpy(r->body + r->len, f->steps + at + 3, n);
        r->len += n; at += 3 + n;
    }
    if (at != f->steps_len) return bad(r, RAS_INVALID);
    uint8_t *s = r->body + r->sub_offset;
    s[4] = f->procedure_status | f->subevent_status << 4;
    s[5] = f->abort_reason; s[7] = r->sub_steps;
    if (f->subevent_status != 1) r->in_subevent = 0;
    if (f->procedure_status != 1 && !r->in_subevent) r->complete = 1;
    return 0;
}
int ras_rx_segment(struct ras_rx *r, const uint8_t *p, unsigned n)
{
    if (!r || !p || n < 2) return RAS_INVALID;
    unsigned first = p[0] & 1, last = p[0] & 2, seq = p[0] >> 2;
    if (r->complete || (first && r->started) || (!first && !r->started) ||
        (!first && seq != r->next_seq)) return RAS_SEQUENCE;
    if (n - 1 > sizeof r->body - r->len) return RAS_OVERFLOW;
    memcpy(r->body + r->len, p + 1, n - 1); r->len += n - 1;
    r->started = 1; r->next_seq = (seq + 1) & 63; r->complete = !!last;
    return r->complete && r->len < 4 ? RAS_INVALID : 0;
}
unsigned ras_segment(const uint8_t *body, unsigned len, unsigned offset, uint8_t seq,
                     unsigned mtu, uint8_t *out, unsigned cap)
{
    if (!body || !out || !len || offset >= len || mtu < 23 || cap < 2) return 0;
    unsigned count = len - offset;
    if (count > mtu - 4) count = mtu - 4;
    if (count > cap - 1) count = cap - 1;
    out[0] = (seq & 63) << 2 | (offset == 0) | ((offset + count == len) ? 2 : 0);
    memcpy(out + 1, body + offset, count);
    return count + 1;
}
