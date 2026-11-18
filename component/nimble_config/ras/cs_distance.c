/* SPDX-License-Identifier: Apache-2.0
 * PBR: arg(PCT_initiator * PCT_reflector) = intercept - 4*pi*f*d/c.
 * Uses adjacent-tone correlation for coarse unwrapping across channel gaps,
 * then least squares and a residual gate. This is not a multipath/MUSIC solver.
 */
#include "cs_distance.h"
#include <math.h>
#include <string.h>
#define PI_F 3.14159265358979323846f
#define DIST_SCALE (299792458.0f / (4.0f * PI_F * 1000000.0f))
static int sign12(unsigned v) { return v & 0x800 ? (int)v - 4096 : (int)v; }
static int tone(const uint8_t *p, int *i, int *q)
{
    /* First tone is antenna path 1; extension slot is deliberately excluded. */
    if (p[0] != 0 || (p[4] & 15) > 1 || (p[4] >> 4) != 0) return 0;
    *i = sign12(p[1] | (p[2] & 15u) << 8);
    *q = sign12(p[2] >> 4 | (unsigned)p[3] << 4);
    return *i != -2048 && *q != -2048 && (*i || *q);
}
static int end(struct cs_distance_result *r, int rc) { r->status = rc; return rc; }
int cs_distance_estimate(const struct ras_record *l, const uint8_t *r, unsigned rn,
                         int32_t offset, struct cs_distance_work *w, struct cs_distance_result *out)
{
    if (!out) return RAS_INVALID;
    memset(out, 0, sizeof *out);
    if (!l || !r || !w || rn < 4 || rn > RAS_MAX_BODY || l->len < 4 ||
        l->len > RAS_MAX_BODY || l->step_count > RAS_MAX_STEPS) return end(out, RAS_INVALID);
    if (!l->complete || l->failed) return end(out, RAS_INCOMPLETE);
    out->counter = l->counter;
    /* Erratum 26610 removes the header's antenna mask. Ignore its Previously
     * Used / RFU octet on receipt (Core Vol 1, Part E, 2.4.1/2.4.2). Step sizes
     * follow our single-path, unfiltered CS configuration, not that octet. */
    if (ras_get16(l->body) != ras_get16(r))
        return end(out, RAS_MISMATCH);
    memset(w, 0, sizeof *w);
    unsigned la = 4, ra = 4, step = 0, subevents = 0;
    while (la < l->len && ra < rn) {
        if (l->len - la < 8 || rn - ra < 8 || ++subevents > 32) return end(out, RAS_INVALID);
        const uint8_t *lh = l->body + la, *rh = r + ra;
        if (ras_get16(lh) != ras_get16(rh) || lh[7] != rh[7]) return end(out, RAS_MISMATCH);
        if ((lh[4] != 0 && lh[4] != 1) || (rh[4] != 0 && rh[4] != 1) || lh[5] || rh[5])
            return end(out, RAS_INCOMPLETE);
        unsigned count = lh[7]; la += 8; ra += 8;
        if (count > 160) return end(out, RAS_INVALID);
        for (unsigned j = 0; j < count; j++, step++) {
            if (la >= l->len || ra >= rn || step >= l->step_count) return end(out, RAS_INVALID);
            uint8_t lm = l->body[la++], rm = r[ra++];
            int ln = ras_step_size(lm, l->role), n = ras_step_size(rm, !l->role);
            if (ln < 0 || n < 0 || (!(lm & 0x80) && (lm & 0x7c)) ||
                (!(rm & 0x80) && (rm & 0x7c))) return end(out, RAS_UNSUPPORTED);
            if ((unsigned)ln > l->len - la || (unsigned)n > rn - ra) return end(out, RAS_INVALID);
            if (!(lm & 0x80) && !(rm & 0x80) && lm != rm) return end(out, RAS_MISMATCH);
            if (lm == 2 && rm == 2) {
                int ai, aq, bi, bq;
                unsigned ch = l->channels[step];
                if (ch > 78) return end(out, RAS_INVALID);
                if (tone(l->body + la, &ai, &aq) && tone(r + ra, &bi, &bq)) {
                    float re = (float)ai * bi - (float)aq * bq;
                    float im = (float)ai * bq + (float)aq * bi;
                    float mag = sqrtf(re * re + im * im);
                    if (mag > 0) { w->re[ch] += re / mag; w->im[ch] += im / mag; out->pairs++; }
                }
            }
            la += ln; ra += n;
        }
        if ((!lh[4] && la != l->len) || (!rh[4] && ra != rn) ||
            (la == l->len && lh[4]) || (ra == rn && rh[4])) return end(out, RAS_INCOMPLETE);
    }
    if (la != l->len || ra != rn || step != l->step_count) return end(out, RAS_MISMATCH);
    float cr = 0, ci = 0; unsigned adjacent = 0, first = 79, last = 0;
    for (unsigned ch = 0; ch < 79; ch++) {
        float mag = sqrtf(w->re[ch] * w->re[ch] + w->im[ch] * w->im[ch]);
        if (mag < 0.1f) { w->re[ch] = w->im[ch] = 0; continue; }
        w->re[ch] /= mag; w->im[ch] /= mag;
        if (first == 79) first = ch;
        last = ch; out->channels++;
        if (ch && (w->re[ch-1] || w->im[ch-1])) {
            cr += w->re[ch] * w->re[ch-1] + w->im[ch] * w->im[ch-1];
            ci += w->im[ch] * w->re[ch-1] - w->re[ch] * w->im[ch-1]; adjacent++;
        }
    }
    out->span_mhz = last > first ? last - first : 0;
    if (out->channels < 15 || out->span_mhz < 30 || adjacent < 8 ||
        sqrtf(cr*cr + ci*ci) < adjacent * 0.5f) return end(out, RAS_QUALITY);
    float coarse = atan2f(ci, cr), coarse_dist = -coarse * DIST_SCALE;
    if (coarse_dist < -0.25f || coarse_dist > 60.0f) return end(out, RAS_QUALITY);
    float sx=0, sy=0, sxx=0, sxy=0, prev_phase=0; unsigned prev=first;
    for (unsigned ch = first; ch <= last; ch++) {
        if (!w->re[ch] && !w->im[ch]) { w->im[ch] = -1; continue; }
        float phase = atan2f(w->im[ch], w->re[ch]);
        if (ch != first) phase += 2 * PI_F * roundf((prev_phase + coarse * (ch - prev) - phase) / (2 * PI_F));
        prev_phase = phase; prev = ch;
        w->re[ch] = phase; w->im[ch] = 1;
        float x = (float)(ch - first);
        sx += x; sy += phase; sxx += x*x; sxy += x*phase;
    }
    float count = out->channels, den = count*sxx - sx*sx;
    if (den <= 0) return end(out, RAS_QUALITY);
    float slope = (count*sxy - sx*sy) / den, intercept = (sy - slope*sx) / count, sum = 0;
    for (unsigned ch = first; ch <= last; ch++) if (w->im[ch] > 0) {
        float e = w->re[ch] - intercept - slope * (ch - first); sum += e*e;
    }
    float residual = sqrtf(sum/count), distance = -slope*DIST_SCALE;
    if (!isfinite(distance) || !isfinite(residual)) return end(out, RAS_QUALITY);
    out->residual_mrad = (uint16_t)fminf(residual * 1000, 65535);
    if (residual > 0.35f || distance < -0.25f || distance > 60.0f) return end(out, RAS_QUALITY);
    out->raw_mm = (int32_t)lroundf(distance * 1000);
    out->distance_mm = out->raw_mm - offset;
    if (out->distance_mm < 0) return end(out, RAS_QUALITY);
    return end(out, 0);
}
