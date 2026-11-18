/* SPDX-License-Identifier: Apache-2.0
 * Telink ROM A/B: keep byte 0x20 erased until the complete image is verified;
 * program the new boot mark BEFORE invalidating the running image's mark.
 * Only permanent activation is supported; this is not an MCUboot trial boot.
 */
#include "ota_storage.h"
#include "mbedtls/sha256.h"
#include <string.h>

TlOtaState tl_ota;
typedef struct { uint32_t magic; TlOtaImage image; uint32_t crc; } Metadata;
#define META_MAGIC 0x3141544fu
_Static_assert(sizeof(Metadata) == 48, "metadata disk layout");

static uint32_t read32(const uint8_t *p)
{ return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static uint32_t other(void) { return tl_ota.active ^ TL_OTA_BANK_SIZE; }
static uint32_t crc32(const void *data, size_t length)
{
    const uint8_t *p = data;
    uint32_t crc = ~0u;
    while (length--) {
        crc ^= *p++;
        for (unsigned i=0; i<8; i++) crc = (crc>>1) ^ (0xedb88320u & (0u-(crc&1)));
    }
    return ~crc;
}
int tl_ota_header(const void *data, size_t length, uint32_t size, uint32_t *build)
{
    const uint8_t *p = data;
    if (length < TL_OTA_HEADER_SIZE || size < 4096 || size > TL_OTA_IMAGE_MAX ||
        read32(p+0x18) != size || read32(p+0x20) != TL_OTA_BOOT_MARK ||
        read32(p+0x28) != TL_OTA_TAG || read32(p+0x30) != TL_OTA_FORMAT ||
        p[0x15] || p[0x16] || p[0x17]) return TL_OTA_INVALID;
    *build = read32(p+0x2c);
    return TL_OTA_OK;
}
static int image_hash(uint32_t base, uint32_t size, uint8_t hash[32])
{
    mbedtls_sha256_context sha;
    uint8_t buffer[256];
    mbedtls_sha256_init(&sha);
    int rc = mbedtls_sha256_starts(&sha, 0);
    for (uint32_t offset=0; !rc && offset<size;) {
        size_t n = size-offset;
        if (n>sizeof buffer) n=sizeof buffer;
        rc = tl_ota_flash_read(base+offset, buffer, n);
        if (offset == 0) buffer[0x20] = 0x4b;
        if (!rc) rc = mbedtls_sha256_update(&sha, buffer, n);
        offset += n;
        if (!(offset & 4095)) tl_ota_flash_yield();
    }
    if (!rc) rc = mbedtls_sha256_finish(&sha, hash);
    mbedtls_sha256_free(&sha);
    return rc ? TL_OTA_IO : 0;
}
static int metadata(Metadata *m)
{
    uint8_t header[TL_OTA_HEADER_SIZE];
    uint32_t build;
    if (tl_ota_flash_read(other()+TL_OTA_META_OFFSET, m, sizeof *m) ||
        tl_ota_flash_read(other(), header, sizeof header)) return TL_OTA_IO;
    if (m->magic != META_MAGIC || m->crc != crc32(&m->image, sizeof m->image) ||
        (header[0x20] != 0xff && header[0x20] != 0x4b)) return TL_OTA_NOT_FOUND;
    header[0x20] = 0x4b;
    if (tl_ota_header(header, sizeof header, m->image.size, &build) || build != m->image.build)
        return TL_OTA_NOT_FOUND;
    return 0;
}
int tl_ota_init(uint32_t active, uint32_t flash_size)
{
    uint8_t header[TL_OTA_HEADER_SIZE];
    memset(&tl_ota, 0, sizeof tl_ota);
    if ((active != 0 && active != TL_OTA_BANK_SIZE) || flash_size < 0x100000u)
        return TL_OTA_INVALID;
    tl_ota.active = active;
    if (tl_ota_flash_read(active, header, sizeof header)) return TL_OTA_IO;
    tl_ota.running.size = read32(header+0x18);
    if (tl_ota_header(header, sizeof header, tl_ota.running.size, &tl_ota.running.build))
        return TL_OTA_INVALID;
    if (image_hash(active, tl_ota.running.size, tl_ota.running.hash)) return TL_OTA_IO;
    Metadata m;
    if (!metadata(&m)) {
        uint8_t mark;
        if (tl_ota_flash_read(other()+0x20, &mark, 1)) return TL_OTA_IO;
        tl_ota.pending = mark == 0x4b;
    }
    tl_ota.ready = 1;
    return 0;
}
int tl_ota_info(unsigned slot, TlOtaImage *info)
{
    if (!tl_ota.ready || slot>1) return TL_OTA_NOT_FOUND;
    if (!slot) { *info=tl_ota.running; return 0; }
    Metadata m;
    int rc=metadata(&m);
    if (!rc) *info=m.image;
    return rc;
}
void tl_ota_abort(void) { tl_ota.uploading=0; }
int tl_ota_begin(uint32_t size, const uint8_t hash[32], const void *first, size_t length)
{
    uint32_t build;
    if (!tl_ota.ready || tl_ota.pending) return TL_OTA_STATE;
    if (!hash || tl_ota_header(first, length, size, &build)) return TL_OTA_INVALID;
    tl_ota.uploading=0;
    /* Invalidate any previous completed transfer before touching its image. */
    if (tl_ota_flash_erase(other()+TL_OTA_META_OFFSET)) return TL_OTA_IO;
    tl_ota.size=size; tl_ota.build=build; tl_ota.offset=0;
    memcpy(tl_ota.expected, hash, 32);
    tl_ota.uploading=1;
    return 0;
}
int tl_ota_write(uint32_t offset, const void *data, size_t length, int last)
{
    const uint8_t *source=data;
    uint8_t page[256], verify[256];
    if (!tl_ota.uploading || offset != tl_ota.offset) return TL_OTA_STATE;
    if (!length || offset>tl_ota.size || length>tl_ota.size-offset ||
        !!last != (length == tl_ota.size-offset)) return TL_OTA_INVALID;
    while (length) {
        size_t n=256-(offset&255);
        if (n>length) n=length;
        if (!(offset&4095) && tl_ota_flash_erase(other()+offset)) goto io_error;
        memcpy(page, source, n);
        if (offset<=0x20 && offset+n>0x20) page[0x20-offset]=0xff;
        if (tl_ota_flash_write(other()+offset, page, n) ||
            tl_ota_flash_read(other()+offset, verify, n) || memcmp(page, verify, n)) goto io_error;
        offset+=n; source+=n; length-=n;
    }
    tl_ota.offset=offset;
    if (last) {
        Metadata m={.magic=0xffffffffu, .image={.size=tl_ota.size, .build=tl_ota.build}};
        tl_ota.uploading=0;
        if (image_hash(other(), m.image.size, m.image.hash)) return TL_OTA_IO;
        if (memcmp(m.image.hash, tl_ota.expected, 32)) return TL_OTA_HASH;
        m.crc=crc32(&m.image, sizeof m.image);
        if (tl_ota_flash_write(other()+TL_OTA_META_OFFSET+4, (uint8_t *)&m+4, sizeof m-4)) return TL_OTA_IO;
        uint32_t magic=META_MAGIC;
        if (tl_ota_flash_write(other()+TL_OTA_META_OFFSET, &magic, sizeof magic)) return TL_OTA_IO;
        Metadata check;
        if (metadata(&check) || memcmp(&check.image, &m.image, sizeof m.image)) return TL_OTA_IO;
    }
    return 0;
io_error:
    tl_ota.uploading=0;
    return TL_OTA_IO;
}
int tl_ota_activate(void)
{
    Metadata m;
    uint8_t hash[32], mark=0x4b, verify;
    if (!tl_ota.ready || tl_ota.uploading || metadata(&m)) return TL_OTA_STATE;
    if (image_hash(other(), m.image.size, hash)) return TL_OTA_IO;
    if (memcmp(hash, m.image.hash, 32)) return TL_OTA_HASH;
    if (tl_ota_flash_write(other()+0x20, &mark, 1) ||
        tl_ota_flash_read(other()+0x20, &verify, 1) || verify != mark) return TL_OTA_IO;
    /* New bank is bootable before the old bank is disabled, including if
     * power disappears between these writes. A bad app still needs SWS;
     * ROM does not provide a trial-boot rollback counter. */
    tl_ota.pending=1;
    mark=0;
    if (tl_ota_flash_write(tl_ota.active+0x20, &mark, 1) ||
        tl_ota_flash_read(tl_ota.active+0x20, &verify, 1) || verify) return TL_OTA_IO;
    return 0;
}
int tl_ota_erase(void)
{
    if (!tl_ota.ready || tl_ota.pending) return TL_OTA_STATE;
    tl_ota.uploading=0; tl_ota.offset=0;
    if (tl_ota_flash_erase(other()+TL_OTA_META_OFFSET) || tl_ota_flash_erase(other())) return TL_OTA_IO;
    return 0;
}
