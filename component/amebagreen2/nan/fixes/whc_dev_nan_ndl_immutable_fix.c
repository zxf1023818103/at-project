/* The pinned vendor constructor copies its committed slots into Immutable
 * Schedule even for an Accepted NDL Response. A later availability shrink
 * then contradicts the constraint sent during setup. Accepted Responses must
 * not add Immutable Schedule; Continue/Request/Confirm paths remain native.
 *
 * Run immediately after construction, before the next attribute and before
 * NDP MIC/CCMP processing. Keep the native dialog, status, NDC/QoS flags, and
 * optional peer/idle fields. The complete vendor archive is pinned by CMake.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

extern uint8_t *__real_rtw_nan_ndl_attri_construct(
    uint8_t *out, void *peer, uint8_t type, uint8_t status, uint32_t *frame_len);

uint8_t *__wrap_rtw_nan_ndl_attri_construct(
    uint8_t *out, void *peer, uint8_t type, uint8_t status, uint32_t *frame_len)
{
    uint8_t *end = __real_rtw_nan_ndl_attri_construct(
        out, peer, type, status, frame_len);
    size_t span, payload, keep, pos, removed;

    if (type != 1 || status != 1 || !out || !end || !frame_len ||
        (uintptr_t)end < (uintptr_t)out)
        return end;
    span = (uintptr_t)end - (uintptr_t)out;
    if (span < 7 || span > 2304 || *frame_len < span || out[0] != 20 ||
        out[4] != 0x11 || !(out[6] & 2))
        return end;
    payload = out[1] | ((size_t)out[2] << 8);
    if (payload + 3 != span)
        return end;
    keep = 7 + ((out[6] & 1) ? 1 : 0) + ((out[6] & 16) ? 2 : 0);
    if (keep >= span)
        return end;
    /* A schedule entry is Map ID, LE16 bitmap control, length, bitmap.
     * If the pinned format ever stops matching, preserve native output.
     */
    for (pos = keep; pos < span;) {
        if (span - pos < 4 || out[pos + 3] > span - pos - 4)
            return end;
        pos += 4 + out[pos + 3];
    }
    removed = span - keep;
    out[6] &= (uint8_t)~2u;
    payload = keep - 3;
    out[1] = (uint8_t)payload;
    out[2] = (uint8_t)(payload >> 8);
    *frame_len -= (uint32_t)removed;
    printf("[NANFIX] omitted Immutable Schedule from Accepted NDL Response\n");
    return out + keep;
}
