#include "nan_usb_wire.h"
#include <string.h>
void nan_usb_frame_init(void *frame, uint32_t sequence)
{
    memset(frame, 0, NAN_USB_FRAME_SIZE);
    struct nan_usb_frame *f = frame;
    f->magic = NAN_USB_MAGIC; f->version = NAN_USB_VERSION;
    f->sequence = sequence; f->used = sizeof(*f);
}
int nan_usb_frame_append(void *frame, const struct nan_usb_record *r, const void *payload)
{
    struct nan_usb_frame *f = frame;
    size_t bytes = sizeof(*r) + ((r->length + 3u) & ~3u);
    if (r->length > NAN_USB_MAX_PAYLOAD || (r->length && !payload) ||
        f->used > NAN_USB_FRAME_SIZE || bytes > NAN_USB_FRAME_SIZE - f->used) return -1;
    memcpy((uint8_t *)frame + f->used, r, sizeof(*r));
    if (r->length) memcpy((uint8_t *)frame + f->used + sizeof(*r), payload, r->length);
    f->used += bytes;
    return 0;
}
int nan_usb_frame_valid(const void *frame, size_t length)
{
    if (!frame || length != NAN_USB_FRAME_SIZE) return 0;
    const struct nan_usb_frame *f = frame;
    if (f->magic != NAN_USB_MAGIC || f->version != NAN_USB_VERSION ||
        f->used < sizeof(*f) || f->used > length || (f->used & 3)) return 0;
    size_t pos = sizeof(*f);
    while (pos < f->used) {
        if (sizeof(struct nan_usb_record) > f->used - pos) return 0;
        const struct nan_usb_record *r = (const void *)((const uint8_t *)frame + pos);
        if (r->type < NAN_USB_RPC || r->type > NAN_USB_ETHERNET || r->length > NAN_USB_MAX_PAYLOAD) return 0;
        size_t bytes = sizeof(*r) + ((r->length + 3u) & ~3u);
        if (bytes > f->used - pos) return 0;
        pos += bytes;
    }
    return pos == f->used;
}
