#ifndef NAN_USB_WIRE_H
#define NAN_USB_WIRE_H
#include <stddef.h>
#include <stdint.h>
#define NAN_USB_FRAME_SIZE 16384u
#define NAN_USB_MAX_PAYLOAD 6144u
#define NAN_USB_MAGIC 0x4e555342u
#define NAN_USB_VERSION 1u
enum nan_usb_type { NAN_USB_RPC = 1, NAN_USB_REPLY, NAN_USB_EVENT, NAN_USB_COMMAND_REPLY, NAN_USB_ETHERNET };
enum nan_usb_command {
    NAN_USB_HELLO = 1, NAN_USB_INIT, NAN_USB_DEINIT, NAN_USB_START, NAN_USB_STOP,
    NAN_USB_VENDOR, NAN_USB_MGMT, NAN_USB_MAC, NAN_USB_STORE_READ, NAN_USB_STORE_WRITE,
    NAN_USB_ECHO
};
/* Both MCUs use 32-bit little-endian words. No pointers cross the USB wire. */
struct nan_usb_frame { uint32_t magic, version, sequence, used; };
struct nan_usb_record { uint16_t type, code; uint32_t id, length; int32_t status; };
_Static_assert(sizeof(struct nan_usb_frame) == 16, "USB frame ABI");
_Static_assert(sizeof(struct nan_usb_record) == 16, "USB record ABI");
void nan_usb_frame_init(void *frame, uint32_t sequence);
int nan_usb_frame_append(void *frame, const struct nan_usb_record *record, const void *payload);
int nan_usb_frame_valid(const void *frame, size_t length);
#endif
