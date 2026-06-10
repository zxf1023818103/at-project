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

#ifdef AMEBAD_VFS_SELFTEST
/*
 * POSIX-fd self-test for the syscall VFS (component/amebad/syscalls): standard C
 * stdio + open/read/write/close routed to LittleFS, and a FreeRTOS socket adapted
 * to a POSIX fd. Gated (off by default) — enabling it keeps the VFS reachable so it
 * is not gc-stripped. Lives in this (XIP-tail-pinned) archive so turning it on does
 * not shift the boot/WiFi code. `make ... VFS_SELFTEST=ON`.
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>          /* open/read/write/close -> our _open/_read/... */
#include <sys/stat.h>
#include "FreeRTOS.h"
#include "FreeRTOS_Sockets.h"

extern int amebad_fd_open_socket(Socket_t s);   /* syscalls/newlib_syscalls.c */

void vfs_selftest(void)
{
    DiagPrintf("\n\r[VFS] ==== POSIX fd self-test ====\n\r");

    /* 1) C stdio (fopen/fwrite/fread) on an LFS path */
    {
        const char *path = "/vfs_stdio.txt";
        const char *msg  = "vfs stdio ok";
        char buf[64];
        size_t n = 0;
        FILE *f = fopen(path, "w");
        if (f != NULL) {
            fwrite(msg, 1, strlen(msg), f);
            fclose(f);
            f = fopen(path, "r");
            if (f != NULL) { n = fread(buf, 1, sizeof(buf) - 1, f); fclose(f); buf[n] = '\0'; }
            DiagPrintf("[VFS] fopen rw '%s' -> %s\n\r", (n ? buf : "?"),
                       (n == strlen(msg) && strcmp(buf, msg) == 0) ? "PASS" : "FAIL");
        } else {
            DiagPrintf("[VFS] fopen(w) FAILED\n\r");
        }
    }

    /* 2) raw open/read/write/close (POSIX fd) on an LFS path */
    {
        const char *path = "/vfs_raw.txt";
        const char *msg  = "vfs raw ok";
        char buf[64];
        int fd, n = -1;
        fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0);
        if (fd >= 0) {
            write(fd, msg, strlen(msg));
            close(fd);
            fd = open(path, O_RDONLY, 0);
            if (fd >= 0) { n = read(fd, buf, sizeof(buf) - 1); close(fd); if (n >= 0) buf[n] = '\0'; }
            DiagPrintf("[VFS] open rw '%s' -> %s\n\r", (n >= 0 ? buf : "?"),
                       (n == (int)strlen(msg) && strcmp(buf, msg) == 0) ? "PASS" : "FAIL");
        } else {
            DiagPrintf("[VFS] open(w) FAILED\n\r");
        }
    }

    /* 3) FreeRTOS socket -> POSIX fd, fstat + close dispatch (no server needed) */
    {
        Socket_t s = FreeRTOS_socket(FREERTOS_AF_INET, FREERTOS_SOCK_STREAM,
                                     FREERTOS_IPPROTO_TCP);
        if (s != FREERTOS_INVALID_SOCKET) {
            int fd = amebad_fd_open_socket(s);
            if (fd >= 0) {
                struct stat st;
                fstat(fd, &st);
                DiagPrintf("[VFS] socket fd=%d S_ISSOCK=%d -> %s\n\r", fd,
                           S_ISSOCK(st.st_mode) ? 1 : 0,
                           S_ISSOCK(st.st_mode) ? "PASS" : "FAIL");
                close(fd);                     /* -> FreeRTOS_closesocket */
                DiagPrintf("[VFS] socket close PASS\n\r");
            } else {
                DiagPrintf("[VFS] amebad_fd_open_socket FAILED\n\r");
                FreeRTOS_closesocket(s);
            }
        } else {
            DiagPrintf("[VFS] FreeRTOS_socket FAILED\n\r");
        }
    }

    DiagPrintf("[VFS] ==== self-test done ====\n\r");
}
#endif /* AMEBAD_VFS_SELFTEST */
