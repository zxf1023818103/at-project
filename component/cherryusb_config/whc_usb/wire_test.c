#include "whc_wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    uint8_t buffer[WHC_USB_MTU + 1], payload[WHC_USB_MTU] = {0};
    struct whc_frame f;
    const uint8_t wifi_on[] = {2,0xa5,0xa5,0xa5, 9,0,0,0, 4,0,0,0, 0,0,0,0, 1,0,0,0};
    payload[0] = 1;
    assert(whc_frame_build(buffer, WHC_API_CALL, 9, payload, 4) == sizeof(wifi_on));
    assert(!memcmp(buffer, wifi_on, sizeof(wifi_on)));
    for (size_t n = 0; n < sizeof(wifi_on); ++n) assert(whc_frame_parse(buffer, n, &f) < 0);
    assert(!whc_frame_parse(buffer, sizeof(wifi_on), &f) && f.code == 9 && f.length == 4);
    for (size_t size = 14; size <= 1514; ++size) {
        assert(whc_frame_build(buffer, WHC_TX_PACKET, 2, payload, size) == size + 16);
        assert(!whc_frame_parse(buffer, size + 16, &f) && f.code == 2 && f.length == size);
        assert(whc_frame_parse(buffer, size + 15, &f) < 0);
    }
    /* Device RX can include DMA padding, but may not overrun the URB. */
    whc_frame_build(buffer, WHC_TX_PACKET, 2, payload, 100);
    whc_put32(buffer, WHC_RX_PACKET); whc_put32(buffer + 12, 16);
    assert(!whc_frame_parse(buffer, 132, &f) && f.data == buffer + 32);
    assert(whc_frame_parse(buffer, 131, &f) < 0);
    whc_put32(buffer + 12, UINT32_MAX);
    assert(whc_frame_parse(buffer, 132, &f) < 0);
    whc_frame_build(buffer, WHC_TX_PACKET, 2, payload, 100);
    memset(buffer + 4, 0xfe, 4);
    assert(!whc_frame_parse(buffer, 116, &f) && f.code == 2); /* unused skb bytes */
    buffer[4] = 3;
    assert(whc_frame_parse(buffer, 116, &f) < 0);
    const uint8_t mac_cmd[] = {0xa5,0xa5,0xa5,0xff,1,2};
    assert(whc_frame_build(buffer, WHC_COMMAND, 0, mac_cmd, 6) == 14);
    assert(!whc_frame_parse(buffer, 14, &f) && f.length == 6);
    assert(!whc_frame_build(buffer, WHC_API_CALL, 1, payload, WHC_USB_MTU - 15));
    assert(whc_frame_build(buffer, WHC_API_CALL, 1, payload, WHC_USB_OUT_MTU - 16) == WHC_USB_OUT_MTU);
    assert(!whc_frame_build(buffer, WHC_API_CALL, 1, payload, WHC_USB_OUT_MTU - 15));
    assert(!whc_frame_build(buffer, WHC_TX_PACKET, 3, payload, 100));
    assert(whc_frame_parse(NULL, 16, &f) < 0);
    /* Deterministic malformed wire-input coverage, under ASan/UBSan. */
    uint32_t rng = 0x12345678;
    for (unsigned test = 0; test < 100000; ++test) {
        for (unsigned i = 0; i < 32; ++i) {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; buffer[i] = rng;
        }
        if (test & 1) whc_put32(buffer, WHC_RX_PACKET + test % 4);
        size_t size = rng % sizeof(buffer);
        if (!whc_frame_parse(buffer, size, &f)) {
            assert(f.data >= buffer && f.data <= buffer + size);
            assert(f.length <= (size_t)(buffer + size - f.data));
        }
    }
    puts("native WHC codec: reference request, all Ethernet sizes, padding, truncation and 100000 malformed frames PASS");
}
