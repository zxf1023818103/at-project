/* The SHA-256-pinned vendor NDC constructor ORs Selected NDC into an
 * uninitialized output byte. Initialize the reserved bits before the frame
 * is authenticated; preserve Selected NDC and all schedule data/lengths.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

extern uint8_t *__real_rtw_nan_ndc_attri_contruct(
    uint8_t *out, void *peer, uint8_t type, uint8_t status, uint32_t *frame_len);

uint8_t *__wrap_rtw_nan_ndc_attri_contruct(
    uint8_t *out, void *peer, uint8_t type, uint8_t status, uint32_t *frame_len)
{
    uint8_t *end = __real_rtw_nan_ndc_attri_contruct(
        out, peer, type, status, frame_len);
    size_t span, payload;
    if (!out || !end || !frame_len || (uintptr_t)end < (uintptr_t)out)
        return end;
    span = (uintptr_t)end - (uintptr_t)out;
    if (span < 10 || span > 2304 || *frame_len < span || out[0] != 19)
        return end;
    payload = out[1] | ((size_t)out[2] << 8);
    if (payload + 3 == span && (out[9] & 0xfe)) {
        out[9] &= 1;
        printf("[NANFIX] cleared NDC Control reserved bits\n");
    }
    return end;
}
