/* Exercise the actual storage implementation with NOR semantics and power
 * cuts at each erase/program stage. No real pairing keys are used. */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../app/nan_store.c"
static uint8_t flash[8192];
static unsigned writes, erases, operation, fail_at;
uint32_t bflb_flash_get_size(void) { return 0x400000; }
static uint8_t *range(uint32_t addr, uint32_t size)
{
    assert(addr >= STORE_BASE && addr - STORE_BASE <= sizeof(flash));
    assert(size <= sizeof(flash) - (addr - STORE_BASE));
    return flash + addr - STORE_BASE;
}
int bflb_flash_read(uint32_t addr, uint8_t *data, uint32_t size)
{ memcpy(data, range(addr, size), size); return 0; }
int bflb_flash_erase(uint32_t addr, uint32_t size)
{
    ++erases;
    int cut = ++operation == fail_at;
    memset(range(addr, size), 255, cut ? size / 2 : size);
    return cut ? -1 : 0;
}
int bflb_flash_write(uint32_t addr, uint8_t *data, uint32_t size)
{
    ++writes;
    int cut = ++operation == fail_at;
    uint8_t *p = range(addr, size);
    for (unsigned i = 0; i < (cut ? size / 2 : size); ++i) p[i] &= data[i];
    return cut ? -1 : 0;
}
static void reboot(void)
{
    free(slots); slots = NULL; active = -1; usable = 0;
    operation = fail_at = 0; bl618dg_nan_store_init();
}
int main(void)
{
    uint8_t a[888], b[888], out[888], original[8192];
    memset(a, 0x36, sizeof(a)); memset(b, 0x79, sizeof(b));
    memset(flash, 255, sizeof(flash)); reboot();
    assert(green2_nan_store(out, sizeof(out), 0) < 0);
    assert(!green2_nan_store(a, sizeof(a), 1));
    reboot(); assert(!green2_nan_store(out, sizeof(out), 0));
    assert(!memcmp(out, a, sizeof(a)));
    unsigned count = writes + erases;
    assert(!green2_nan_store(a, sizeof(a), 1) && writes + erases == count);
    memcpy(original, flash, sizeof(flash));
    for (unsigned cut = 1; cut <= 3; ++cut) {
        memcpy(flash, original, sizeof(flash)); reboot(); fail_at = cut;
        assert(green2_nan_store(b, sizeof(b), 1) < 0);
        reboot(); assert(!green2_nan_store(out, sizeof(out), 0));
        assert(!memcmp(out, a, sizeof(a)));
    }
    assert(!green2_nan_store(b, sizeof(b), 1));
    reboot(); assert(!green2_nan_store(out, sizeof(out), 0) && !memcmp(out, b, sizeof(b)));
    /* Corrupt newest CRC: older committed record is still readable. */
    flash[STORE_SECTOR + 12] ^= 1; reboot();
    assert(!green2_nan_store(out, sizeof(out), 0) && !memcmp(out, a, sizeof(a)));
    /* Unknown nonblank media is preserved, never autoformatted. */
    memset(flash, 0x77, sizeof(flash)); reboot(); count = writes + erases;
    assert(green2_nan_store(a, sizeof(a), 1) < 0 && count == writes + erases);
    free(slots);
    puts("NAN journal: reboot, duplicate writes, three power cuts, CRC fallback, unknown-data preservation PASS");
}
