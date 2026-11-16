/* Read-only migration from a Green2 full Flash backup. Never prints key data.
 * cc -DLFS_READONLY -Icomponent/littlefs_config/littlefs this_file.c
 *    component/littlefs_config/littlefs/{lfs,lfs_util}.c -o extract_pairing */
#include "lfs.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static FILE *image;
static int read_block(const struct lfs_config *c, lfs_block_t b, lfs_off_t o, void *p, lfs_size_t n)
{
    if (b >= c->block_count || o > c->block_size || n > c->block_size - o) return LFS_ERR_IO;
    if (fseek(image, 0x300000 + b * c->block_size + o, SEEK_SET)) return LFS_ERR_IO;
    return fread(p, 1, n, image) == n ? 0 : LFS_ERR_IO;
}
static int sync_image(const struct lfs_config *c) { (void)c; return 0; }
int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: %s green2-full-4m.bin pairing.bin\n", argv[0]); return 2; }
    image = fopen(argv[1], "rb");
    if (!image || fseek(image, 0, SEEK_END) || ftell(image) != 0x400000) return 1;
    struct lfs_config cfg = {.read=read_block, .sync=sync_image, .read_size=256, .prog_size=256,
        .block_size=4096, .block_count=240, .block_cycles=500, .cache_size=512, .lookahead_size=256};
    lfs_t fs;
    int rc = lfs_mount(&fs, &cfg);
    if (rc) { fprintf(stderr, "LittleFS mount: %d\n", rc); return 1; }
    lfs_file_t file;
    rc = lfs_file_open(&fs, &file, "/nan-pairing", LFS_O_RDONLY);
    if (rc) { fprintf(stderr, "Pairing file open: %d\n", rc); return 1; }
    uint8_t data[1024];
    lfs_soff_t size = lfs_file_size(&fs, &file);
    if (size != 888 || lfs_file_read(&fs, &file, data, size) != size || memcmp(data, "GNP1", 4) || data[4] != 1) {
        fprintf(stderr, "Pairing format/size check failed (size=%d)\n", (int)size); return 1;
    }
    lfs_file_close(&fs, &file); lfs_unmount(&fs); fclose(image);
    FILE *out = fopen(argv[2], "wbx");
    if (!out || fwrite(data, 1, size, out) != (size_t)size || fclose(out)) return 1;
    memset(data, 0, sizeof(data));
    printf("Pairing record extracted: %d bytes (no keys logged)\n", (int)size);
    return 0;
}
