/* Experimental native-WHC profile: two 4 KiB journal slots at the start of
 * the BL618DG SDK's unused media partition. The bridge profile still uses
 * Green2 LittleFS. Do not use this reservation with a media filesystem. */
#include "FreeRTOS.h"
#include "semphr.h"
#include "bflb_flash.h"
#include "whc_wire.h"
#include <stdio.h>
#include <string.h>
#define STORE_BASE 0x378000u
#define STORE_SECTOR 4096u
#define STORE_MAX 1024u
static uint8_t *slots;
static int active = -1, usable;
static SemaphoreHandle_t lock;
static uint32_t crc_byte(uint32_t c, uint8_t value)
{
    c ^= value;
    for (unsigned i = 0; i < 8; ++i) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    return c;
}
static uint32_t checksum(const uint8_t *p, size_t size)
{
    uint32_t c = ~0u;
    for (unsigned i = 4; i < 12; ++i) c = crc_byte(c, p[i]);
    for (size_t i = 0; i < size; ++i) c = crc_byte(c, p[16 + i]);
    return ~c;
}
static int valid(const uint8_t *p)
{
    size_t size = whc_get32(p + 8);
    return !memcmp(p, "BNP1", 4) && size && size <= STORE_MAX && whc_get32(p + 12) == checksum(p, size);
}
static int erased(const uint8_t *p)
{
    for (unsigned i = 0; i < STORE_SECTOR; ++i) if (p[i] != 0xff) return 0;
    return 1;
}
void bl618dg_nan_store_init(void)
{
    lock = xSemaphoreCreateMutex(); slots = pvPortMalloc(2 * STORE_SECTOR);
    configASSERT(lock && slots);
    if (bflb_flash_get_size() != 0x400000 || bflb_flash_read(STORE_BASE, slots, 2 * STORE_SECTOR)) {
        printf("[NAN] pairing flash read/geometry failed\n"); return;
    }
    for (unsigned i = 0; i < 2; ++i) {
        const uint8_t *p = slots + i * STORE_SECTOR;
        if (valid(p) && (active < 0 || (int32_t)(whc_get32(p + 4) -
            whc_get32(slots + active * STORE_SECTOR + 4)) > 0)) active = i;
    }
    usable = active >= 0 || (erased(slots) && erased(slots + STORE_SECTOR));
    printf("[NAN] host pairing journal %s\n", active >= 0 ? "loaded" : usable ? "empty" : "invalid; preserved");
}
int green2_nan_store(void *data, size_t size, int write)
{
    if (!data || !size || size > STORE_MAX || !usable || xSemaphoreTake(lock, pdMS_TO_TICKS(5000)) != pdPASS) return -1;
    int rc = -1;
    uint8_t *current = active < 0 ? NULL : slots + active * STORE_SECTOR;
    if (!write) {
        if (current && whc_get32(current + 8) == size) { memcpy(data, current + 16, size); rc = 0; }
    } else if (current && whc_get32(current + 8) == size && !memcmp(current + 16, data, size)) {
        rc = 0; /* Cache updates often repeat: do not erase unchanged keys. */
    } else {
        unsigned next = active == 0 ? 1 : 0;
        uint8_t *p = slots + next * STORE_SECTOR;
        uint32_t addr = STORE_BASE + next * STORE_SECTOR;
        uint32_t generation = current ? whc_get32(current + 4) + 1 : 1;
        memset(p, 0xff, STORE_SECTOR);
        whc_put32(p + 4, generation); whc_put32(p + 8, size);
        memcpy(p + 16, data, size); whc_put32(p + 12, checksum(p, size));
        /* Commit magic last. The other valid slot survives loss of power. */
        /* Every program source must be RAM: the HAL temporarily disables XIP. */
        memcpy(p, "BNP1", 4);
        if (!bflb_flash_erase(addr, STORE_SECTOR) && !bflb_flash_write(addr + 4, p + 4, 12 + size) &&
            !bflb_flash_write(addr, p, 4) && !bflb_flash_read(addr, p, STORE_SECTOR) &&
            valid(p) && whc_get32(p + 8) == size && !memcmp(p + 16, data, size)) {
            active = next; rc = 0;
        }
    }
    xSemaphoreGive(lock);
    return rc;
}
