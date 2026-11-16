#include "whc_wire.h"
#include <string.h>
uint32_t whc_get32(const void *data)
{
    const uint8_t *p = data;
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
void whc_put32(void *data, uint32_t n)
{
    uint8_t *p = data;
    for (unsigned i = 0; i < 4; ++i) p[i] = n >> (8 * i);
}
int whc_frame_parse(const void *buffer, size_t length, struct whc_frame *out)
{
    const uint8_t *p = buffer;
    if (!p || !out || length < 8 || length > WHC_USB_MTU) return -1;
    uint32_t event = whc_get32(p), code = whc_get32(p + 4);
    size_t offset = WHC_HEADER_SIZE, size;
    if (event == WHC_COMMAND) {
        offset = 8; size = code; code = 0;
    } else {
        if (length < WHC_HEADER_SIZE) return -1;
        size = whc_get32(p + 8);
        if (event == WHC_RX_PACKET || event == WHC_TX_PACKET) {
            /* Full-MAC firmware only writes wlan_idx, data_len and pad_len.
             * Its reserved/WHCH aggregation bytes can contain old skb data. */
            code = p[4] & 3u;
            if (code > 2) return -1;
            uint32_t pad = whc_get32(p + 12);
            if (pad > length - WHC_HEADER_SIZE) return -1;
            offset += pad;
            if (size < 14 || size > 1514) return -1;
        } else if (event != WHC_API_CALL && event != WHC_API_REPLY) return -1;
    }
    if (size > length - offset) return -1;
    /* Some native device messages include alignment padding at the end. */
    *out = (struct whc_frame){event, code, p + offset, size};
    return 0;
}
size_t whc_frame_build(void *buffer, uint32_t event, uint32_t code,
                       const void *data, size_t length)
{
    size_t header = event == WHC_COMMAND ? 8 : WHC_HEADER_SIZE;
    if (!buffer || length > WHC_USB_OUT_MTU - header || (length && !data)) return 0;
    if (event != WHC_COMMAND && event != WHC_API_CALL && event != WHC_TX_PACKET) return 0;
    if (event == WHC_TX_PACKET && (code > 2 || length < 14 || length > 1514)) return 0;
    uint8_t *p = buffer;
    memset(p, 0, header);
    whc_put32(p, event);
    whc_put32(p + 4, event == WHC_COMMAND ? length : code);
    if (header == WHC_HEADER_SIZE) whc_put32(p + 8, length);
    if (length) memcpy(p + header, data, length);
    return header + length;
}
