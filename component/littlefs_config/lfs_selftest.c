/*
 * LittleFS self-test. Mounts the on-flash filesystem via the platform VFS hook
 * (vfs_lfs_mount), lists the root, reads back the files baked into lfs.bin (the
 * prefilled image from project/resource/), and does a write + read-back round trip.
 * Output via printf (the platform libc retarget — AmebaD: LOGUART). Called once
 * from the bring-up thread (scheduler running — the flash device lock + the LFS
 * mutex need FreeRTOS).
 *
 * NOTE: a firmware-only image (no lfs.bin) leaves the region blank; lfs_config then
 * auto-formats, so the prefilled-file reads report "not found" but the write/
 * read-back still PASSes. Flash the 4 MB merge with lfs.bin to exercise the
 * prefilled reads.
 */
#include <string.h>
#include <stdio.h>          /* printf */

#include "lfs.h"
#include "lfs_config.h"     /* LFS_CFG_REGION_OFFSET / BLOCK_COUNT / BLOCK_SIZE */

/* The mounted LittleFS singleton, from the platform's VFS hook (the component/
 * syscall_config contract; AmebaD's component/amebad/lfs_port supplies the strong def). */
extern lfs_t *vfs_lfs_mount(void);

static void lfs_dump_file(lfs_t *lfs, const char *path)
{
    lfs_file_t f;
    char buf[96];
    lfs_ssize_t n;

    if (lfs_file_open(lfs, &f, path, LFS_O_RDONLY) < 0) {
        printf("[LFS] read %s -> NOT FOUND\n\r", path);
        return;
    }
    n = lfs_file_read(lfs, &f, buf, sizeof(buf) - 1);
    lfs_file_close(lfs, &f);
    if (n < 0) {
        printf("[LFS] read %s -> error %d\n\r", path, (int)n);
        return;
    }
    buf[n] = '\0';
    printf("[LFS] read %s (%d B): '%s'\n\r", path, (int)n, buf);
}

void lfs_selftest(void)
{
    lfs_t *lfs;

    printf("\n\r[LFS] ==== LittleFS self-test ====\n\r");

    lfs = vfs_lfs_mount();
    if (lfs == NULL) {
        printf("[LFS] mount FAILED\n\r");
        return;
    }
    printf("[LFS] mounted (region 0x%x, %u x %u KB)\n\r",
           (unsigned)LFS_CFG_REGION_OFFSET,
           (unsigned)LFS_CFG_BLOCK_COUNT,
           (unsigned)(LFS_CFG_BLOCK_SIZE / 1024u));

    /* 1) list the root directory */
    {
        lfs_dir_t dir;
        struct lfs_info info;
        if (lfs_dir_open(lfs, &dir, "/") >= 0) {
            printf("[LFS] root listing:\n\r");
            while (lfs_dir_read(lfs, &dir, &info) > 0) {
                if (info.type == LFS_TYPE_DIR) {
                    printf("[LFS]   <dir> %s\n\r", info.name);
                } else {
                    printf("[LFS]   %d B  %s\n\r", (int)info.size, info.name);
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
        printf("[LFS] write+readback %s -> %s\n\r", path, ok ? "PASS" : "FAIL");
    }

    printf("[LFS] ==== self-test done ====\n\r");
}

#ifdef AMEBAD_VFS_SELFTEST
/*
 * POSIX-fd self-test for the syscall VFS (component/syscall_config): standard C
 * stdio + open/read/write/close routed to LittleFS, and a FreeRTOS socket adapted
 * to a POSIX fd. Gated (off by default) — enabling it keeps the VFS reachable so it
 * is not gc-stripped. Lives in liblfs_config.a (XIP-tail-pinned) so turning it on
 * does not shift the boot/WiFi code. `make ... VFS_SELFTEST=ON`.
 */
#include <fcntl.h>
#include <unistd.h>          /* open/read/write/close -> our _open/_read/... */
#include <sys/stat.h>
#include "FreeRTOS.h"
#include "FreeRTOS_Sockets.h"
#include "syscalls.h"           /* fd_open_socket() — component/syscall_config */

void vfs_selftest(void)
{
    printf("\n\r[VFS] ==== POSIX fd self-test ====\n\r");

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
            printf("[VFS] fopen rw '%s' -> %s\n\r", (n ? buf : "?"),
                       (n == strlen(msg) && strcmp(buf, msg) == 0) ? "PASS" : "FAIL");
        } else {
            printf("[VFS] fopen(w) FAILED\n\r");
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
            printf("[VFS] open rw '%s' -> %s\n\r", (n >= 0 ? buf : "?"),
                       (n == (int)strlen(msg) && strcmp(buf, msg) == 0) ? "PASS" : "FAIL");
        } else {
            printf("[VFS] open(w) FAILED\n\r");
        }
    }

    /* 3) FreeRTOS socket -> POSIX fd, fstat + close dispatch (no server needed) */
    {
        Socket_t s = FreeRTOS_socket(FREERTOS_AF_INET, FREERTOS_SOCK_STREAM,
                                     FREERTOS_IPPROTO_TCP);
        if (s != FREERTOS_INVALID_SOCKET) {
            int fd = fd_open_socket(s);
            if (fd >= 0) {
                struct stat st;
                fstat(fd, &st);
                printf("[VFS] socket fd=%d S_ISSOCK=%d -> %s\n\r", fd,
                           S_ISSOCK(st.st_mode) ? 1 : 0,
                           S_ISSOCK(st.st_mode) ? "PASS" : "FAIL");
                close(fd);                     /* -> FreeRTOS_closesocket */
                printf("[VFS] socket close PASS\n\r");
            } else {
                printf("[VFS] fd_open_socket FAILED\n\r");
                FreeRTOS_closesocket(s);
            }
        } else {
            printf("[VFS] FreeRTOS_socket FAILED\n\r");
        }
    }

    printf("[VFS] ==== self-test done ====\n\r");
}
#endif /* AMEBAD_VFS_SELFTEST */
