/* Experimental adapter for the SHA-256-pinned Green2 vendor NDP object.
 *
 * The NANDOW decoder stores its requested cipher at response[30], but
 * rtw_nan_ndp_response_prehandle+0x8e overwrites entry[289] with SK-128 (1).
 * The frame constructor later reads that byte when building the response's
 * Cipher Suite Information Attribute. Keep the library's processing intact
 * and correct only a successful PASN-128 response before frame construction
 * (and its Shared-Key MIC calculation). PSK/open/failed paths are unchanged.
 *
 * Offsets are recovered from rtw_nan_data_ndp.o, not a public API. The build
 * must verify the complete object's hash before enabling this wrapper.
 */
#include <stdint.h>
#include <stdio.h>

enum {
    RESPONSE_CIPHER = 30,
    ENTRY_TYPE_STATUS = 12,
    ENTRY_CIPHER = 289,
    ENTRY_FLAGS = 435,
    ENTRY_REJECTED = 1,
    ENTRY_SECURITY = 4,
    CIPHER_SK_128 = 1,
    CIPHER_PASN_128 = 7,
    NDP_REJECTED = 2
};

extern void *__real_rtw_nan_ndp_response_prehandle(void *peer, void *response);

void *__wrap_rtw_nan_ndp_response_prehandle(void *peer, void *response)
{
    uint8_t *entry;
    uint8_t requested_cipher;

    if (!peer || !response)
        return NULL;
    requested_cipher = ((const uint8_t *)response)[RESPONSE_CIPHER];
    entry = __real_rtw_nan_ndp_response_prehandle(peer, response);
    if (entry && requested_cipher == CIPHER_PASN_128 &&
        entry[ENTRY_CIPHER] == CIPHER_SK_128 &&
        (entry[ENTRY_FLAGS] & (ENTRY_REJECTED | ENTRY_SECURITY)) == ENTRY_SECURITY &&
        (entry[ENTRY_TYPE_STATUS] >> 4) != NDP_REJECTED) {
        entry[ENTRY_CIPHER] = CIPHER_PASN_128;
        printf("[NANFIX] NDP responder cipher 1 -> 7\n");
    }
    return entry;
}
