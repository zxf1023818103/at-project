/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../storage/flash_layout.h"

#define TL_OTA_BANK_SIZE TL_FLASH_BANK
#define TL_OTA_IMAGE_MAX TL_FLASH_IMAGE_MAX
#define TL_OTA_META_OFFSET TL_FLASH_META
#define TL_OTA_SECTOR_SIZE TL_FLASH_SECTOR
#define TL_OTA_HEADER_SIZE 52u
#define TL_OTA_TAG 0x32334f54u
#define TL_OTA_FORMAT 0x00010001u
#define TL_OTA_BOOT_MARK 0x544c4e4bu

enum { TL_OTA_OK, TL_OTA_INVALID, TL_OTA_IO, TL_OTA_STATE, TL_OTA_HASH, TL_OTA_NOT_FOUND };
typedef struct { uint32_t size, build; uint8_t hash[32]; } TlOtaImage;
typedef struct {
    uint32_t active, size, offset, build;
    uint8_t expected[32];
    TlOtaImage running;
    unsigned ready, uploading, pending;
} TlOtaState;
extern TlOtaState tl_ota;

/* Physical flash addresses, never XIP addresses. Writes are page-bounded. */
int tl_ota_flash_read(uint32_t address, void *data, size_t length);
int tl_ota_flash_write(uint32_t address, const void *data, size_t length);
int tl_ota_flash_erase(uint32_t address);
void tl_ota_flash_yield(void);

int tl_ota_init(uint32_t active, uint32_t flash_size);
int tl_ota_header(const void *data, size_t length, uint32_t size, uint32_t *build);
int tl_ota_info(unsigned slot, TlOtaImage *info);
int tl_ota_begin(uint32_t size, const uint8_t hash[32], const void *first, size_t length);
int tl_ota_write(uint32_t offset, const void *data, size_t length, int last);
int tl_ota_activate(void);
int tl_ota_erase(void);
void tl_ota_abort(void);
