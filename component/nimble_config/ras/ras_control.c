/* SPDX-License-Identifier: Apache-2.0 */
#include "ras_control.h"
#include "ras_codec.h"
int ras_control_request(const uint8_t *p, unsigned n, unsigned state,
                        int available, uint16_t counter, uint8_t reply[2])
{
    reply[0] = 2; reply[1] = 3;
    if (!p || !n) return RAS_REPLY;
    if (state != RAS_SERVER_IDLE && p[0] != 3 && !(state == RAS_SERVER_ACK && p[0] == 1)) {
        reply[1] = 7; return RAS_REPLY;
    }
    if (p[0] == 3) {
        if (n != 1) return RAS_REPLY;
        reply[1] = state == RAS_SERVER_IDLE ? 5 : 1;
        return state == RAS_SERVER_IDLE ? RAS_REPLY : RAS_ABORT;
    }
    if (p[0] > 1) { reply[1] = 2; return RAS_REPLY; }
    if (n != 3 || (p[2] & 0xf0)) return RAS_REPLY;
    if (!available || ras_get16(p + 1) != (counter & 0xfff)) { reply[1] = 8; return RAS_REPLY; }
    reply[1] = 1;
    return p[0] ? RAS_ACK : RAS_GET;
}
