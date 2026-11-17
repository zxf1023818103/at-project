#include "peer_rpa.h"
#include <string.h>

int green2_recover_peer_rpa(uint8_t peer_type, const uint8_t identity[6],
                          uint8_t peer_rpa[6], green2_read_peer_rpa read_rpa)
{
    static const uint8_t zero[6];
    uint8_t recovered[6] = {0};
    /* 2/3 are public/random identity addresses resolved by the controller.
     * SC f5/f6 require the actual over-the-air RPA, not this identity. */
    if ((peer_type != 2 && peer_type != 3) || memcmp(peer_rpa, zero, 6) ||
        !memcmp(identity, zero, 6) || !read_rpa) return 0;
    if (read_rpa(peer_type - 2, identity, recovered) ||
        (recovered[5] & 0xc0) != 0x40) return 0;
    /* The random part of an RPA must not be all zeroes or all ones. */
    uint32_t random = recovered[3] | (uint32_t)recovered[4] << 8 |
                      (uint32_t)(recovered[5] & 0x3f) << 16;
    if (!random || random == 0x3fffff) return 0;
    memcpy(peer_rpa, recovered, 6);
    return 1;
}
