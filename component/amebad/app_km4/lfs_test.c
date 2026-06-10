/*
 * LittleFS self-test (KM4). Mounts the on-flash filesystem via the AmebaD
 * lfs-port, lists the root, reads back the files baked into lfs.bin (the
 * prefilled image from project/resource/, merged at 0x300000 by `make -C project
 * amebad`), and does a write + read-back round trip. Output on the LOGUART
 * console. Called once from the WiFi bring-up thread (scheduler running — the
 * flash device lock + the LFS mutex need FreeRTOS).
 *
 * NOTE: a firmware-only image (`make -C project/amebad`, no lfs.bin) leaves the
 * region blank; the port then auto-formats, so the prefilled-file reads report
 * "not found" but the write/read-back still PASSes. Flash build/amebad/Image_All.bin
 * (the 4 MB merge with lfs.bin) to exercise the prefilled reads.
 */
#include <string.h>

#include "ameba_soc.h"      /* DiagPrintf */
#include "lfs.h"
#include "lfs_port.h"

static void lfs_dump_file(lfs_t *lfs, const char *path)
{
    lfs_file_t f;
    char buf[96];
    lfs_ssize_t n;

    if (lfs_file_open(lfs, &f, path, LFS_O_RDONLY) < 0) {
        DiagPrintf("[LFS] read %s -> NOT FOUND\n\r", path);
        return;
    }
    n = lfs_file_read(lfs, &f, buf, sizeof(buf) - 1);
    lfs_file_close(lfs, &f);
    if (n < 0) {
        DiagPrintf("[LFS] read %s -> error %d\n\r", path, (int)n);
        return;
    }
    buf[n] = '\0';
    DiagPrintf("[LFS] read %s (%d B): '%s'\n\r", path, (int)n, buf);
}

void lfs_selftest(void)
{
    lfs_t *lfs;

    DiagPrintf("\n\r[LFS] ==== LittleFS self-test ====\n\r");

    lfs = amebad_lfs_mount();
    if (lfs == NULL) {
        DiagPrintf("[LFS] mount FAILED\n\r");
        return;
    }
    DiagPrintf("[LFS] mounted (region 0x300000, 256 x 4 KB)\n\r");

    /* 1) list the root directory */
    {
        lfs_dir_t dir;
        struct lfs_info info;
        if (lfs_dir_open(lfs, &dir, "/") >= 0) {
            DiagPrintf("[LFS] root listing:\n\r");
            while (lfs_dir_read(lfs, &dir, &info) > 0) {
                /* DiagPrintf (ROM printf) supports only %d/%x/%s — no %u or
                 * width specifiers (%5u misparses, and the following %s then
                 * reads a bogus vararg -> fault). Use plain %d. */
                if (info.type == LFS_TYPE_DIR) {
                    DiagPrintf("[LFS]   <dir> %s\n\r", info.name);
                } else {
                    DiagPrintf("[LFS]   %d B  %s\n\r", (int)info.size, info.name);
                }
            }
            lfs_dir_close(lfs, &dir);
        }
    }

    /* 2) read the prefilled files from lfs.bin (project/resource/) */
    lfs_dump_file(lfs, "/KV/test_key_1");
    lfs_dump_file(lfs, "/README.md");

    /* 3) write + read-back round trip (proves prog/erase work) */
    {
        const char *path = "/selftest.txt";
        const char *msg  = "amebad lfs rw ok";
        lfs_file_t f;
        char buf[64];
        lfs_ssize_t n = -1;
        int ok = 0;

        if (lfs_file_open(lfs, &f, path,
                          LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) >= 0) {
            lfs_file_write(lfs, &f, msg, strlen(msg));
            lfs_file_close(lfs, &f);   /* flush to flash */

            if (lfs_file_open(lfs, &f, path, LFS_O_RDONLY) >= 0) {
                n = lfs_file_read(lfs, &f, buf, sizeof(buf) - 1);
                lfs_file_close(lfs, &f);
            }
            if (n == (lfs_ssize_t)strlen(msg)) {
                buf[n] = '\0';
                ok = (strcmp(buf, msg) == 0);
            }
        }
        DiagPrintf("[LFS] write+readback %s -> %s\n\r", path, ok ? "PASS" : "FAIL");
    }

    DiagPrintf("[LFS] ==== self-test done ====\n\r");
}
