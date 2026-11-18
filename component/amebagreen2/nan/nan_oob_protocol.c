#include "nan_oob_protocol.h"
#include <string.h>

int nan_oob_pin(uint32_t entropy, char pin[7])
{
    /* Rejection sampling avoids modulo bias in the six decimal digits. */
    if (entropy >= UINT32_C(4294000000)) return -1;
    unsigned value = entropy % 1000000;
    for (int i = 5; i >= 0; --i) { pin[i] = '0' + value % 10; value /= 10; }
    pin[6] = 0;
    return 0;
}

void nan_oob_packet(uint8_t out[NAN_OOB_PACKET_SIZE], unsigned state,
                    unsigned seconds, const char pin[7], const uint8_t id[6])
{
    memset(out, 0, NAN_OOB_PACKET_SIZE);
    memcpy(out, "GNO1", 4);
    if (state == NAN_OOB_READY && (!seconds || seconds > NAN_OOB_WINDOW_SECONDS))
        state = NAN_OOB_CLOSED;
    out[4] = state;
    memcpy(out + 13, id, 6);
    if (state == NAN_OOB_READY) {
        out[5] = seconds; out[6] = seconds >> 8;
        memcpy(out + 7, pin, 6);
    }
}
