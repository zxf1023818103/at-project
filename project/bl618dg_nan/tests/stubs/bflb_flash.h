#pragma once
#include <stdint.h>
uint32_t bflb_flash_get_size(void);
int bflb_flash_read(uint32_t, uint8_t *, uint32_t);
int bflb_flash_write(uint32_t, uint8_t *, uint32_t);
int bflb_flash_erase(uint32_t, uint32_t);
