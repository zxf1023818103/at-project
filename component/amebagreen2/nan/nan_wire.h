#ifndef GREEN2_NAN_WIRE_H
#define GREEN2_NAN_WIRE_H
#include <stddef.h>
#include <stdint.h>
int green2_nan_secure_ndp(const uint8_t *attrs, size_t len,
                         uint8_t id, const uint8_t ndi[6], uint8_t *dialog_token);
#endif
