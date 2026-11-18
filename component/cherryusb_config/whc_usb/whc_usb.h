#pragma once
#include "whc_wire.h"
typedef void (*whc_usb_receive_fn)(const struct whc_frame *);
void whc_usb_init(uintptr_t base, whc_usb_receive_fn receive);
int whc_usb_connected(void);
int whc_usb_send(uint32_t event, uint32_t code, const void *data, size_t length);
int whc_usb_call(uint32_t api, const void *data, size_t length);
void whc_usb_status(void);
