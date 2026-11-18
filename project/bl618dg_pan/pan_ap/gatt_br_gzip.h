#pragma once
#include <stdbool.h>
#include <stdint.h>

void gatt_gzip_init(void);
bool gatt_gzip_start(unsigned seconds, unsigned profile, unsigned baud, unsigned level, unsigned codec);
void gatt_gzip_stop(void);
/* Called on BTU only. Retains a fragment until accepted by L2CAP. */
unsigned gatt_gzip_peek(const uint8_t **data);
void gatt_gzip_accept(unsigned length);
unsigned gatt_gzip_status(uint8_t *data);
