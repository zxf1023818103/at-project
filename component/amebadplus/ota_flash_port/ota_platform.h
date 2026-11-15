#pragma once
#include <stdint.h>
#define DPLUS_OTA1 0x010000u
#define DPLUS_OTA2 0x190000u
#define DPLUS_SLOT_SIZE 0x170000u
#define DPLUS_SECTOR_SIZE 4096u
int ota_platform_active_slot(void);
int ota_platform_begin(void);
void ota_platform_end(void);
int ota_platform_read(uint32_t off, void *buf, uint32_t len);
int ota_platform_validate_layout(uint32_t off, uint32_t len);
int ota_platform_write(uint32_t off, const void *buf, uint32_t len);
int ota_platform_erase(uint32_t off);
void ota_platform_reset(void);
