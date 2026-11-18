#ifndef NAN_USB_H
#define NAN_USB_H
#include "nan_usb_wire.h"
typedef void (*nan_usb_receive_fn)(const struct nan_usb_record *, const void *);
void nan_usb_init(uintptr_t base, nan_usb_receive_fn receive);
int nan_usb_connected(void);
int nan_usb_send(uint16_t type, uint16_t code, uint32_t id, int32_t status, const void *data, size_t length);
void nan_usb_status(void);
#if NAN_USB_HOST
int nan_usb_rpc(uint16_t command, const void *input, size_t input_size, void *output, size_t *output_size);
#endif
#endif
