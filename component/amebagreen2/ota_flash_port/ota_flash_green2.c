/* Green2 certificate + manifest + KM4NS/KM4TZ writer. The certificate magic is
 * withheld until a complete flash readback verifies; the running slot remains
 * valid until that commit succeeds. Both HTTP streams and AWS random blocks
 * use this same bounds-checked writer. The caller owns the shared OTA lock. */
#include "ota_flash.h"
#include "ota_platform.h"
#include <stdio.h>
#include <string.h>

static ota_ctx_t *session;
static uint32_t header_seen, payload_end;
static uint8_t signature_seen;
static uint8_t erased[(GREEN2_SLOT_SIZE / GREEN2_SECTOR_SIZE + 7) / 8];
static const uint8_t magic[8] = "81958711";

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int fail(void)
{
    ota_platform_end();
    session = NULL;
    return -1;
}
void ota_flash_on_lock_released(void) { (void)fail(); }

static int erase_sector(ota_ctx_t *c, uint32_t offset)
{
    uint32_t index = offset / GREEN2_SECTOR_SIZE;
    if (index >= GREEN2_SLOT_SIZE / GREEN2_SECTOR_SIZE) return fail();
    if (!(erased[index / 8] & (1u << (index % 8)))) {
        if (ota_platform_erase(c->write_off + index * GREEN2_SECTOR_SIZE)) return fail();
        erased[index / 8] |= 1u << (index % 8);
    }
    return 0;
}
int ota_flash_begin(ota_ctx_t *c)
{
    uint8_t sig[8];
    if (!c || session) return -1;
    int slot = ota_platform_active_slot();
    if (slot != 0 && slot != 1) return -1;
    memset(c, 0, sizeof(*c));
    c->active_off = slot ? GREEN2_OTA2 : GREEN2_OTA1;
    c->write_off = slot ? GREEN2_OTA1 : GREEN2_OTA2;
    if (ota_platform_read(c->active_off, sig, 8) || memcmp(sig, magic, 8)) return -1;
    if (ota_platform_begin()) return -1;
    session = c;
    header_seen = payload_end = signature_seen = 0;
    memset(erased, 0, sizeof(erased));
    /* Invalidate the inactive certificate before programming any other sector. */
    if (erase_sector(c, 0)) return -1;
    if (ota_platform_read(c->write_off, sig, 8)) return fail();
    for (unsigned i = 0; i < 8; ++i) if (sig[i] != 0xff) return fail();
    printf("[OTA] active=0x%06lx destination=0x%06lx\n",
           (unsigned long)c->active_off, (unsigned long)c->write_off);
    return 0;
}
int ota_flash_begin_random(ota_ctx_t *c) { return ota_flash_begin(c); }

static int parse_header(ota_ctx_t *c)
{
    if (le32(c->hdr + 4) != 1 || memcmp(c->hdr + 8, "OTA1", 4) ||
        le32(c->hdr + 12) != 24 || le32(c->hdr + 24) != 32 ||
        le32(c->hdr + 28) != 1) return fail();
    c->image_len = le32(c->hdr + 20);
    c->hdr_checksum = le32(c->hdr + 16);
    if (c->image_len < 0x2000 + 256 || c->image_len > GREEN2_SLOT_SIZE ||
        payload_end > c->image_len) return fail();
    c->have_hdr = 1;
    return 0;
}
int ota_flash_write_at(ota_ctx_t *c, uint32_t off, const uint8_t *data, uint32_t len)
{
    if (!c || session != c || (!data && len) || off > GREEN2_SLOT_SIZE + 32 ||
        len > GREEN2_SLOT_SIZE + 32 - off) return fail();
    if (c->have_hdr && (off > c->image_len + 32 || len > c->image_len + 32 - off)) return fail();
    while (off < 32 && len) {
        uint32_t bit = 1u << off;
        if ((header_seen & bit) && c->hdr[off] != *data) return fail();
        c->hdr[off] = *data++;
        header_seen |= bit;
        ++off; --len;
    }
    if (header_seen == UINT32_MAX && !c->have_hdr) {
        if (parse_header(c)) return -1;
        c->hdr_got = 32;
    }
    if (!len) return 0;
    uint32_t pos = off - 32;
    if (c->have_hdr && (pos > c->image_len || len > c->image_len - pos)) return fail();
    if (payload_end < pos + len) payload_end = pos + len;
    while (pos < 8 && len) {
        if ((signature_seen & (1u << pos)) && c->sig[pos] != *data) return fail();
        c->sig[pos] = *data++;
        signature_seen |= 1u << pos;
        ++pos; --len;
    }
    while (len) {
        uint32_t n = GREEN2_SECTOR_SIZE - pos % GREEN2_SECTOR_SIZE;
        if (n > len) n = len;
        if (erase_sector(c, pos) || ota_platform_write(c->write_off + pos, data, n)) return fail();
        pos += n; data += n; len -= n;
    }
    return 0;
}
int ota_flash_feed(ota_ctx_t *c, const uint8_t *data, uint32_t len)
{
    if (!c || session != c) return -1;
    uint32_t off = c->written;
    if (ota_flash_write_at(c, off, data, len)) return -1;
    c->written += len; /* Download offset, including the 32-byte transport header. */
    return 0;
}
int ota_flash_read_image(const ota_ctx_t *c, uint32_t off, uint8_t *out, uint32_t len)
{
    if (!c || session != c || !c->have_hdr || (!out && len) ||
        off > c->image_len + 32 || len > c->image_len + 32 - off) return -1;
    while (off < 32 && len) { *out++ = c->hdr[off++]; --len; }
    if (!len) return 0;
    uint32_t pos = off - 32;
    while (pos < 8 && len) { *out++ = c->sig[pos++]; --len; }
    return len ? ota_platform_read(c->write_off + pos, out, len) : 0;
}
static int read_payload(ota_ctx_t *c, uint32_t pos, uint8_t *out, uint32_t len)
{
    return ota_flash_read_image(c, pos + 32, out, len);
}
int ota_flash_finish_random(ota_ctx_t *c)
{
    uint8_t buf[256];
    uint32_t sum = 0;
    if (!c || session != c || !c->have_hdr || signature_seen != 0xff ||
        memcmp(c->sig, magic, 8) || ota_platform_validate_layout(c->write_off,c->image_len)) return fail();
    for (uint32_t pos = 0; pos < c->image_len;) {
        uint32_t n = c->image_len - pos;
        if (n > sizeof(buf)) n = sizeof(buf);
        if (read_payload(c, pos, buf, n)) return fail();
        for (uint32_t i = 0; i < n; ++i) sum += buf[i];
        pos += n;
    }
    if (sum != c->hdr_checksum) {
        printf("[OTA] checksum mismatch: %08lx != %08lx\n", (unsigned long)sum, (unsigned long)c->hdr_checksum);
        return fail();
    }
    if (ota_platform_write(c->write_off, c->sig, 8) ||
        ota_platform_read(c->write_off, buf, 8) || memcmp(buf, magic, 8)) return fail();
    /* Commit was read back. Clearing the old certificate selects the new bank
     * even when versions are equal; the old image body is left intact. */
    const uint8_t zero[8] = {0};
    if (ota_platform_write(c->active_off, zero, 8) ||
        ota_platform_read(c->active_off, buf, 8) || memcmp(buf, zero, 8)) return fail();
    c->checksum = sum;
    c->written = c->image_len;
    printf("[OTA] verified and armed slot 0x%06lx\n", (unsigned long)c->write_off);
    ota_platform_end();
    session = NULL;
    return 0;
}
int ota_flash_finish(ota_ctx_t *c)
{
    if (!c || session != c || !c->have_hdr || c->written != c->image_len + 32) return fail();
    return ota_flash_finish_random(c);
}
void ota_flash_reboot(void)
{
    ota_platform_reset();
    for (;;) {}
}
