/* Adapt the reference GREEN2_NAN_AUTH_PEER_FIX to the FreeRTOS NP ABI.
 * A cached service lets the phone send Auth1 before a new-MAC Subscribe.
 * The native PASN handler otherwise drops it when peer_info_get returns NULL.
 *
 * This wrapper is reached only after rtw_on_auth has selected interface 2
 * and rtw_is_nan_frame. Allocate an ordinary UNAUTHENTICATED vendor peer;
 * native RSN/PASN, identity, key and replay checks still decide acceptance.
 * Vendor station limits, timers and teardown own this allocation, exactly
 * as for a Subscribe. No frame is retained and no new timer/queue is added.
 * The archive builder pins the vendor ABI and leaves original members intact.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern uint8_t *rtw_nan_peer_info_get(const uint8_t *address);
extern uint8_t *rtw_nan_peer_info_alloc(const uint8_t *address);
extern uint8_t __real_rtw_nan_pasn_rx_checking(uint8_t *address,
                                            uint8_t *frame, uint16_t length);

static int early_auth1(const uint8_t *address, const uint8_t *frame, size_t length)
{
    static const uint8_t zero[6];
    /* Only plain, unfragmented Auth1. Retry is allowed. Special/protected
     * forms and later transactions remain the native handler's responsibility.
     * Length/null checks are performed by the caller before any access here.
     */
    if (frame[0] != 0xb0 || (frame[1] & 0xc7) || (frame[22] & 0x0f) ||
        (frame[4] & 1) || (address[0] & 1) ||
        !memcmp(address, zero, sizeof(zero)) ||
        !memcmp(frame + 4, zero, sizeof(zero)) ||
        memcmp(address, frame + 10, 6) ||
        frame[24] != 7 || frame[25] || frame[26] != 1 || frame[27] ||
        frame[28] || frame[29] || length == 30)
        return 0;
    /* Do not allocate on truncated IE framing. The original authentication
     * stack validates the contents and required RSN/PASN elements afterward.
     */
    for (size_t pos = 30; pos < length;) {
        if (length - pos < 2 || frame[pos + 1] > length - pos - 2)
            return 0;
        pos += 2u + frame[pos + 1];
    }
    return 1;
}

uint8_t __wrap_rtw_nan_pasn_rx_checking(uint8_t *address,
                                     uint8_t *frame, uint16_t length)
{
    if (!address || !frame || length < 30 || length > 2304)
        return 0;
    if (early_auth1(address, frame, length) && !rtw_nan_peer_info_get(address)) {
        uint8_t *peer = rtw_nan_peer_info_alloc(address);
        printf("[NANFIX] early Auth1 peer initialized=%u\n", peer != NULL);
    }
    return __real_rtw_nan_pasn_rx_checking(address, frame, length);
}
