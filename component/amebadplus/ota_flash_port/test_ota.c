#include "image_layout.h"
/* Host NOR simulation: boundaries, interrupted writes and bank commit order. */
#include "ota_flash.h"
#include "ota_platform.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char flash[0x400000];
static int slot, fail_commit, fail_erase;
static unsigned writes;
int ota_platform_active_slot(void) { return slot; }
int ota_platform_begin(void) { return 0; }
void ota_platform_end(void) {}
void ota_platform_reset(void) { abort(); }
int ota_platform_read(uint32_t off, void *p, uint32_t n)
{
    assert(off <= sizeof(flash) && n <= sizeof(flash) - off);
    memcpy(p, flash + off, n); return 0;
}
static int read_record(void *arg, uint32_t pos, uint32_t logical, uint8_t out[32])
{
    (void)logical;
    return ota_platform_read(*(uint32_t *)arg+pos,out,32);
}
int ota_platform_validate_layout(uint32_t off, uint32_t len)
{
#ifdef AT_ENABLE_TFM
    return dplus_image_layout(read_record,&off,len,1);
#else
    return dplus_image_layout(read_record,&off,len,0);
#endif
}
int ota_platform_write(uint32_t off, const void *p, uint32_t n)
{
    assert(off < 0x300000 && n <= 0x300000 - off);
    if (fail_commit && off == (slot ? DPLUS_OTA1 : DPLUS_OTA2)) return -1;
    const unsigned char *d = p;
    for (unsigned i = 0; i < n; ++i) flash[off+i] &= d[i];
    ++writes; return 0;
}
int ota_platform_erase(uint32_t off)
{
    assert(off % 4096 == 0);
    assert(off >= (slot ? DPLUS_OTA1 : DPLUS_OTA2));
    assert(off < (slot ? DPLUS_OTA1 : DPLUS_OTA2) + DPLUS_SLOT_SIZE);
    if (!fail_erase) memset(flash + off, 0xff, 4096);
    return 0;
}
static void reset(int active)
{
    ota_flash_on_lock_released();
    memset(flash, 0xff, sizeof(flash));
    memcpy(flash + DPLUS_OTA1, "81958711", 8);
    memcpy(flash + DPLUS_OTA2, "81958711", 8);
    memset(flash + 0x300000, 0xa5, 0x100000);
    slot = active; writes = fail_commit = fail_erase = 0;
}
static void intact(void)
{
    assert(!memcmp(flash + (slot ? DPLUS_OTA2 : DPLUS_OTA1), "81958711", 8));
    assert(flash[0x300000] == 0xa5 && flash[sizeof(flash)-1] == 0xa5);
}
static void committed(const unsigned char *data, unsigned len)
{
    unsigned dst = slot ? DPLUS_OTA1 : DPLUS_OTA2;
    unsigned src = slot ? DPLUS_OTA2 : DPLUS_OTA1;
    assert(!memcmp(flash + dst, data + 32, len - 32));
    for (unsigned i = 0; i < 8; ++i) assert(flash[src+i] == 0);
    for (unsigned i = 0x300000; i < sizeof(flash); ++i) assert(flash[i] == 0xa5);
}
int main(int argc, char **argv)
{
    assert(argc == 2 || argc == 3);
    FILE *f = fopen(argv[1], "rb"); assert(f);
    fseek(f, 0, SEEK_END); unsigned len = ftell(f); rewind(f);
    unsigned char *data = malloc(len); assert(data);
    assert(fread(data, 1, len, f) == len); fclose(f);
    ota_ctx_t c;
    if (argc == 3) {
        reset(0); assert(ota_flash_begin(&c) == 0);
        assert(ota_flash_feed(&c, data, len) == 0);
        assert(ota_flash_finish(&c) < 0); intact();
        free(data); puts("PASS: invalid layout rejected before commit"); return 0;
    }
    const unsigned chunks[] = {1, 7, 31, 1023, 4096};
    for (unsigned k = 0; k < sizeof(chunks)/sizeof(chunks[0]); ++k) {
        reset(k % 2); assert(ota_flash_begin(&c) == 0);
        for (unsigned off = 0; off < len;) {
            unsigned n = chunks[k]; if (n > len-off) n = len-off;
            assert(ota_flash_feed(&c, data+off, n) == 0); off += n;
            intact();
        }
        assert(ota_flash_finish(&c) == 0); committed(data, len);
    }
    reset(1); assert(ota_flash_begin_random(&c) == 0);
    /* Reverse blocks, header last, with an idempotent retransmission. */
    for (unsigned end = len; end;) {
        unsigned off = end > 512 ? end-512 : 0;
        assert(ota_flash_write_at(&c, off, data+off, end-off) == 0);
        assert(ota_flash_write_at(&c, off, data+off, end-off) == 0);
        end = off; intact();
    }
    assert(ota_flash_finish_random(&c) == 0); committed(data, len);
    reset(0); assert(ota_flash_begin(&c) == 0);
    assert(ota_flash_feed(&c, data, len-1) == 0);
    assert(ota_flash_finish(&c) < 0); intact();
    reset(0); assert(ota_flash_begin(&c) == 0);
    assert(ota_flash_write_at(&c, UINT32_MAX-1, data, 8) < 0); intact();
    reset(0); assert(ota_flash_begin(&c) == 0);
    data[16] ^= 1; assert(ota_flash_feed(&c, data, len) == 0);
    assert(ota_flash_finish(&c) < 0); intact(); data[16] ^= 1;
    reset(0); assert(ota_flash_begin(&c) == 0);
    data[28] = 2; assert(ota_flash_feed(&c, data, len) < 0); intact(); data[28] = 1;
    reset(1); assert(ota_flash_begin(&c) == 0);
    assert(ota_flash_feed(&c, data, len) == 0); fail_commit = 1;
    assert(ota_flash_finish(&c) < 0); intact();
    reset(0); fail_erase = 1; assert(ota_flash_begin(&c) < 0); intact();
    reset(3); assert(ota_flash_begin(&c) < 0); assert(writes == 0);
    free(data);
    puts("PASS: streaming, reverse blocks, retries, bounds, checksum, truncation, erase and commit failures");
    return 0;
}
