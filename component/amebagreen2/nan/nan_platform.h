#ifndef NAN_PLATFORM_H
#define NAN_PLATFORM_H
#include <stddef.h>
#include <stdint.h>
/* The shared FreeRTOS NAN host uses either local WHC IPC or the USB bridge. */
int green2_nan_radio_enable(void);
int green2_nan_radio_ready(void);
int green2_nan_radio_start(void);
int green2_nan_send(const void *data, size_t len);
#endif
