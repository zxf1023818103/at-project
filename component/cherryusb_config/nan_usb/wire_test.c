#include "nan_usb_wire.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
int main(void)
{
    uint8_t *frame = malloc(NAN_USB_FRAME_SIZE);
    uint8_t payload[NAN_USB_MAX_PAYLOAD];
    assert(frame);
    for (size_t i = 0; i < sizeof(payload); ++i) payload[i] = (uint8_t)(i * 37);
    nan_usb_frame_init(frame, 42);
    assert(nan_usb_frame_valid(frame, NAN_USB_FRAME_SIZE));
    struct nan_usb_record record = {NAN_USB_ETHERNET, 0, 0, 1514, 0};
    unsigned count = 0;
    while (!nan_usb_frame_append(frame, &record, payload)) ++count;
    assert(count == 10 && nan_usb_frame_valid(frame, NAN_USB_FRAME_SIZE));
    assert(!nan_usb_frame_valid(frame, NAN_USB_FRAME_SIZE - 1));
    struct nan_usb_frame *f = (void *)frame;
    f->used = NAN_USB_FRAME_SIZE + 4;
    assert(!nan_usb_frame_valid(frame, NAN_USB_FRAME_SIZE));
    nan_usb_frame_init(frame, 43);
    record = (struct nan_usb_record){NAN_USB_RPC, NAN_USB_ECHO, 17, NAN_USB_MAX_PAYLOAD, 0};
    assert(!nan_usb_frame_append(frame, &record, payload));
    assert(nan_usb_frame_valid(frame, NAN_USB_FRAME_SIZE));
    assert(!memcmp(frame + sizeof(*f) + sizeof(record), payload, sizeof(payload)));
    struct nan_usb_record *r = (void *)(frame + sizeof(*f));
    r->length = UINT32_MAX;
    assert(!nan_usb_frame_valid(frame, NAN_USB_FRAME_SIZE));
    r->length = 1; r->type = 0;
    assert(!nan_usb_frame_valid(frame, NAN_USB_FRAME_SIZE));
    nan_usb_frame_init(frame, 44);
    record.length = 1;
    assert(nan_usb_frame_append(frame, &record, NULL) < 0);
    assert(!nan_usb_frame_append(frame, &record, payload));
    assert(f->used == 36 && nan_usb_frame_valid(frame, NAN_USB_FRAME_SIZE));
    f->version++;
    assert(!nan_usb_frame_valid(frame, NAN_USB_FRAME_SIZE));
    free(frame);
    puts("USB aggregate framing, boundaries and malformed records PASS");
    return 0;
}
