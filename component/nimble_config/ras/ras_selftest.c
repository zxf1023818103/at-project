/* SPDX-License-Identifier: Apache-2.0 */
#include "ras_selftest.h"
#include <math.h>
#include <string.h>
static void pct(uint8_t *p, float phase)
{
    unsigned i = ((int)lroundf(1500 * cosf(phase))) & 0xfff;
    unsigned q = ((int)lroundf(1500 * sinf(phase))) & 0xfff;
    p[0] = i; p[1] = (i >> 8) | (q << 4); p[2] = q >> 4;
}
int ras_synthetic_pair(struct ras_record *l, struct ras_rx *r, float meters)
{
    ras_record_reset(l); memset(r, 0, sizeof *r);
    struct ras_fragment f = {.counter=37, .start_acl_event=9, .reference_power=-40,
        .paths=1, .num_steps=1, .procedure_status=1, .subevent_status=1};
    uint8_t raw[12] = {2,0,9};
    unsigned count = 0;
    for (unsigned ch = 2; ch <= 76; ch++) {
        if (ch >= 23 && ch <= 25) continue;
        raw[1] = ch; raw[7] = 0; raw[11] = 0x10; /* extension slot flag */
        float common = 0.13f * ch;
        pct(raw + 4, common);
        f.steps = raw; f.steps_len = sizeof raw; f.continuation = count != 0;
        if (ch == 76) f.procedure_status = f.subevent_status = 0;
        int rc = ras_record_append(l, &f, 0, 0);
        if (rc) return rc;
        uint8_t *dest = r->body + 12 + count * 10;
        dest[0] = 2; memcpy(dest + 1, raw + 3, 9);
        pct(dest + 2, -common - 12.566370614359172f * meters * (2402 + ch) * 1000000.0f / 299792458.0f);
        count++;
    }
    memcpy(r->body, l->body, 12);
    r->len = 12 + count*10; r->complete = 1;
    return 0;
}
int ras_selftest(struct ras_record *l, struct ras_rx *r, struct cs_distance_work *w,
                 struct cs_distance_result *result)
{
    int rc = ras_synthetic_pair(l, r, 2.5f);
    if (rc) return rc;
    rc = cs_distance_estimate(l, r->body, r->len, 0, w, result);
    if (rc) return rc;
    return result->raw_mm < 2490 || result->raw_mm > 2510 ? RAS_QUALITY : 0;
}
