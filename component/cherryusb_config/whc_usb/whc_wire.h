#pragma once
#include <stddef.h>
#include <stdint.h>

/* Ameba native WHC USB framing (whc_common/whc_def.h). No MCU pointers. */
#define WHC_USB_MTU 1600u
/* Selected original SDK firmware arms its OUT buffers for 1536 bytes. */
#define WHC_USB_OUT_MTU 1536u
#define WHC_HEADER_SIZE 16u
#define WHC_TX_PACKET 0xa5a5a500u
#define WHC_RX_PACKET 0xa5a5a501u
#define WHC_API_CALL  0xa5a5a502u
#define WHC_API_REPLY 0xa5a5a503u
#define WHC_COMMAND   0xa5a5a504u
struct whc_frame {
    uint32_t event, code;
    const uint8_t *data;
    size_t length;
};
uint32_t whc_get32(const void *p);
void whc_put32(void *p, uint32_t n);
int whc_frame_parse(const void *buffer, size_t length, struct whc_frame *out);
size_t whc_frame_build(void *buffer, uint32_t event, uint32_t code,
                       const void *data, size_t length);
