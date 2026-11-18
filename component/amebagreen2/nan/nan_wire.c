#include "nan_wire.h"
#include <string.h>

/* NDP/NDPE share the first 11 bytes (Wi-Fi Aware Tables 82/86).
 * The pinned rtw_cfgvendor_nan_event_hdr_datapath_request_received() leaves
 * its event.security byte zero. Validate the actual received attributes. */
int green2_nan_secure_ndp(const uint8_t *attrs, size_t len,
                         uint8_t id, const uint8_t ndi[6], uint8_t *dialog_token)
{
    unsigned found = 0;
    uint8_t token = 0;
    if (!attrs || !ndi) return 0;
    while (len) {
        if (len < 3) return 0;
        size_t n = attrs[1] | ((size_t)attrs[2] << 8);
        if (n > len - 3) return 0;
        if (attrs[0] == 0x10 || attrs[0] == 0x29) {
            const uint8_t *ndp = attrs + 3;
            unsigned bit = attrs[0] == 0x10 ? 1 : 2;
            if ((found & bit) || n < 11 || ndp[1] != 0 || ndp[9] != id ||
                !(ndp[10] & (1 << 2)) || memcmp(ndp + 3, ndi, 6)) return 0;
            if (found && ndp[0] != token) return 0;
            token = ndp[0];
            found |= bit;
        }
        attrs += n + 3; len -= n + 3;
    }
    if (found && dialog_token) *dialog_token = token;
    return found != 0;
}
