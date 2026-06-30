/*
 * fs_cli.c — LittleFS file commands for the AmebaD KM4 FreeRTOS-Plus-CLI console:
 *   ls cat rm rmdir mkdir mv cp touch hexdump df mkfs chmod
 *   md5sum sha1sum sha224sum sha256sum sha384sum sha512sum   (when FS_CLI_HASH)
 *
 * Registered into the running CLI list from main() after amebad_monitor_start(),
 * exactly like aws_cli.c — so the shared component/amebad/monitor stays untouched
 * and core-agnostic. KM4-only: the POSIX VFS (open/opendir/...) and LittleFS sit
 * behind newlib libc, which KM0 (Cortex-M23, -nostdlib) does not have.
 *
 * Everything rides the existing POSIX VFS (component/syscall_config/newlib_syscalls.c):
 * open/read/write/close/lseek/stat/unlink/rename + opendir/readdir/closedir +
 * mkdir/rmdir. Paths pass verbatim to littlefs (no cwd; "/" is the root).
 *
 * chmod/ls store+show POSIX permission bits as a LittleFS custom attribute
 * (FS_ATTR_MODE) — LittleFS has no native mode field — via lfs_setattr/lfs_getattr,
 * the same direct-lfs path df/mkfs already use.
 *
 * The hash commands (FS_CLI_HASH) call the standard upstream Mbed TLS module API
 * (mbedcrypto, component/mbedtls_config) — NOT ROM mbedTLS — so a future
 * MBEDTLS_SHA256_ALT etc. (component/amebad/mbedtls_port) can back them with the ROM
 * mbedtls_*_process / IPSEC HW engine with no change here. They are compiled out in
 * a ROMKAT=ON build (which links ROM mbedTLS instead, ABI-incompatible with upstream).
 *
 * Output policy (matches the monitor's run_rom_cmd idiom): commands whose output can
 * exceed the 2 KB CLI buffer (ls/cat/hexdump) stream straight to the LOGUART via
 * DiagPutChar and leave the CLI buffer empty; short status lines use snprintf(out,...).
 * Direct writes also skip the per-line log timestamp, which is what a raw dump wants.
 */
#include <stdio.h>
#include <stdlib.h>          /* strtol, strtoul */
#include <string.h>
#include <fcntl.h>           /* open, O_RDONLY/O_WRONLY/O_CREAT/O_TRUNC */
#include <unistd.h>          /* read, write, close, unlink, rmdir */
#include <sys/stat.h>        /* stat, mkdir */
#include <dirent.h>          /* opendir, readdir, closedir, DT_DIR (syscall_config's) */

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"
#include "ameba_soc.h"       /* DiagPutChar */

#include "lfs_config.h"      /* lfs_t, lfs_fs_size, LFS_CFG_BLOCK_*, lfs_config_format,
                              * lfs_setattr/lfs_getattr, LFS_ERR_NOATTR (via lfs.h) */
#include "syscalls.h"        /* vfs_lfs_mount */

#ifdef FS_CLI_HASH
/* Upstream Mbed TLS streaming hash modules (mbedcrypto). Headers are already on the
 * include path via amebad_mbedtls_headers; symbols resolve to the linked mbedcrypto. */
#include <mbedtls/md5.h>
#include <mbedtls/sha1.h>
#include <mbedtls/sha256.h>
#include <mbedtls/sha512.h>
#endif

#define FS_PATH_MAX   256

/* LittleFS user-attribute type id holding the POSIX permission bits (stored as a
 * uint16_t, low 12 bits). 'm' for "mode"; the only custom attr the repo uses. */
#define FS_ATTR_MODE  ((uint8_t)'m')

/* One scratch buffer shared by cat/cp/hexdump. Safe as a static: only the single
 * console (cli) task ever runs these handlers, one command at a time. */
static uint8_t s_fsbuf[512];

/* Copy CLI parameter `idx` (1-based; idx 0 = the command word) into `buf`. Parses the raw command
 * line honoring double-quotes, so paths with spaces work: `rm "my file.txt"`. Returns the length, or
 * -1 if the parameter is absent/empty. (We scan the raw string rather than FreeRTOS_CLIGetParameter,
 * which tokenizes on every space and can't represent a spaced filename. The path commands are also
 * registered with a variable parameter count so the CLI doesn't reject the quoted token as "extra".)
 */
static int fs_arg(const char *cmd, UBaseType_t idx, char *buf, size_t sz)
{
    const char *p = cmd;
    UBaseType_t cur = 0;

    for (;;) {
        const char *start;
        int len;

        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            return -1;
        }
        if (*p == '"') {                        /* quoted token: spaces allowed inside */
            p++;
            start = p;
            while (*p != '\0' && *p != '"') {
                p++;
            }
            len = (int)(p - start);
            if (*p == '"') {
                p++;
            }
        } else {                                /* bare token: ends at whitespace */
            start = p;
            while (*p != '\0' && *p != ' ' && *p != '\t') {
                p++;
            }
            len = (int)(p - start);
        }
        if (cur == idx) {
            if (len <= 0) {
                return -1;
            }
            if ((size_t)len >= sz) {
                len = (int)(sz - 1);
            }
            memcpy(buf, start, (size_t)len);
            buf[len] = '\0';
            return len;
        }
        cur++;
    }
}

/* Raw console output (LOGUART, same path cli_puts uses). snprintf into a local then
 * fs_emit avoids relying on the ROM DiagPrintf's limited format support. */
static void fs_emit(const char *s)
{
    while (*s) {
        DiagPutChar(*s++);
    }
}

static void fs_emit_n(const char *s, int n)
{
    while (n-- > 0) {
        DiagPutChar(*s++);
    }
}

/* True while an SMP (BLE) file/image transfer is in flight. Strong override in smp_img_ota.c (MCUMGR
 * builds); weak stub here returns 0 otherwise. A console flash op during a transfer masks interrupts
 * (FLASH_Write_Lock cpsid i) long enough to drop HCI UART bytes and crash the BLE H4 RX parser, so
 * the flash commands below decline while a transfer is active. */
int smp_xfer_active(void) __attribute__((weak));
int smp_xfer_active(void) { return 0; }

#define FS_GUARD_XFER()                                                        \
    do {                                                                       \
        if (smp_xfer_active()) {                                               \
            fs_emit("busy: BLE file transfer in progress; retry shortly\r\n"); \
            return pdFALSE;                                                    \
        }                                                                      \
    } while (0)

/* Join dir + "/" + name (handles dir == "" or "/"). Manual bounded copy rather than
 * snprintf("%s/%s") so it never overruns and doesn't trip -Wformat-truncation (both
 * dir and name can be up to NAME_MAX). A pathological over-long path just truncates;
 * the subsequent stat() then fails and ls omits the size — benign. */
static void join_path(char *dst, size_t sz, const char *dir, const char *name)
{
    size_t n = 0;

    if (sz == 0) {
        return;
    }
    if (!(dir[0] == '\0' || (dir[0] == '/' && dir[1] == '\0'))) {
        while (dir[n] != '\0' && n < sz - 1) {
            dst[n] = dir[n];
            n++;
        }
    }
    if (n < sz - 1) {
        dst[n++] = '/';
    }
    while (*name != '\0' && n < sz - 1) {
        dst[n++] = *name++;
    }
    dst[n] = '\0';
}

/* Read the FS_ATTR_MODE attribute for `path`, or synthesize a default (0755 dir /
 * 0644 file) when the entry was never chmod'd (lfs_getattr -> LFS_ERR_NOATTR) or the
 * filesystem is unavailable. Returns the 12 low permission bits. */
static uint16_t fs_mode_of(lfs_t *lfs, const char *path, int is_dir)
{
    uint16_t m;

    if (lfs != NULL &&
        lfs_getattr(lfs, path, FS_ATTR_MODE, &m, sizeof m) == (lfs_ssize_t)sizeof m) {
        return (uint16_t)(m & 07777);
    }
    return is_dir ? 0755 : 0644;
}

/* Format a 10-char "drwxr-xr-x" permission string (NUL-terminated). Renders the rwx
 * triplets; the set-uid/gid/sticky bits are stored but not displayed. */
static void fs_mode_str(char b[11], uint16_t mode, int is_dir)
{
    static const char rwx[8][4] = { "---", "--x", "-w-", "-wx",
                                    "r--", "r-x", "rw-", "rwx" };
    b[0] = is_dir ? 'd' : '-';
    memcpy(b + 1, rwx[(mode >> 6) & 7], 3);
    memcpy(b + 4, rwx[(mode >> 3) & 7], 3);
    memcpy(b + 7, rwx[ mode       & 7], 3);
    b[10] = '\0';
}

/* ----------------------------------------------------------------------- ls --- */
static BaseType_t cmd_ls(char *out, size_t outlen, const char *cmd)
{
    FS_GUARD_XFER();
    char path[FS_PATH_MAX], child[FS_PATH_MAX], num[24], perm[11];
    const char *dir;
    lfs_t *lfs;
    DIR *d;
    struct dirent *e;

    out[0] = '\0';
    dir = (fs_arg(cmd, 1, path, sizeof path) > 0) ? path : "/";
    lfs = vfs_lfs_mount();

    d = opendir(dir);
    if (d == NULL) {
        snprintf(out, outlen, "ls: %s: cannot open\r\n", dir);
        return pdFALSE;
    }
    while ((e = readdir(d)) != NULL) {
        int is_dir = (e->d_type == DT_DIR);
        long size = 0;

        join_path(child, sizeof child, dir, e->d_name);
        if (!is_dir) {
            struct stat st;
            if (stat(child, &st) == 0) {
                size = (long)st.st_size;
            }
        }
        fs_mode_str(perm, fs_mode_of(lfs, child, is_dir), is_dir);
        /* "<perm>  <size>  <name>[/]" — streamed piecewise (long names never need a
         * big line buffer). */
        fs_emit(perm);
        snprintf(num, sizeof num, "  %10ld  ", size);
        fs_emit(num);
        fs_emit(e->d_name);
        fs_emit(is_dir ? "/\r\n" : "\r\n");
    }
    closedir(d);
    return pdFALSE;
}

/* ---------------------------------------------------------------------- cat --- */
static BaseType_t cmd_cat(char *out, size_t outlen, const char *cmd)
{
    FS_GUARD_XFER();
    char path[FS_PATH_MAX];
    int fd, n;

    out[0] = '\0';
    if (fs_arg(cmd, 1, path, sizeof path) <= 0) {
        snprintf(out, outlen, "usage: cat <file>\r\n");
        return pdFALSE;
    }
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        snprintf(out, outlen, "cat: %s: not found\r\n", path);
        return pdFALSE;
    }
    while ((n = read(fd, (char *)s_fsbuf, sizeof s_fsbuf)) > 0) {
        fs_emit_n((const char *)s_fsbuf, n);
    }
    close(fd);
    fs_emit("\r\n");
    return pdFALSE;
}

/* ------------------------------------------------------------------ hexdump --- */
static BaseType_t cmd_hexdump(char *out, size_t outlen, const char *cmd)
{
    FS_GUARD_XFER();
    char path[FS_PATH_MAX], arg[16], line[96];
    long max = -1;
    unsigned long off = 0;
    int fd, n, done = 0;

    out[0] = '\0';
    if (fs_arg(cmd, 1, path, sizeof path) <= 0) {
        snprintf(out, outlen, "usage: hexdump <file> [max-bytes]\r\n");
        return pdFALSE;
    }
    if (fs_arg(cmd, 2, arg, sizeof arg) > 0) {
        max = strtol(arg, NULL, 0);
    }
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        snprintf(out, outlen, "hexdump: %s: not found\r\n", path);
        return pdFALSE;
    }
    while (!done && (n = read(fd, (char *)s_fsbuf, sizeof s_fsbuf)) > 0) {
        int i;
        for (i = 0; i < n; i += 16) {
            int j, p, len = n - i;
            if (len > 16) {
                len = 16;
            }
            if (max >= 0 && off >= (unsigned long)max) { done = 1; break; }
            if (max >= 0 && off + len > (unsigned long)max) {
                len = (int)((unsigned long)max - off);
            }
            p = snprintf(line, sizeof line, "%08lx  ", off);
            for (j = 0; j < 16; j++) {
                if (j < len) {
                    p += snprintf(line + p, sizeof line - p, "%02x ", s_fsbuf[i + j]);
                } else {
                    p += snprintf(line + p, sizeof line - p, "   ");
                }
                if (j == 7) {
                    line[p++] = ' ';
                }
            }
            line[p++] = ' ';
            line[p++] = '|';
            for (j = 0; j < len; j++) {
                unsigned char c = s_fsbuf[i + j];
                line[p++] = (c >= 0x20 && c < 0x7f) ? (char)c : '.';
            }
            line[p++] = '|';
            line[p++] = '\r';
            line[p++] = '\n';
            line[p]   = '\0';
            fs_emit(line);
            off += len;
        }
    }
    close(fd);
    return pdFALSE;
}

/* ------------------------------------------------------------ rm/rmdir/mkdir --- */
static BaseType_t cmd_rm(char *out, size_t outlen, const char *cmd)
{
    FS_GUARD_XFER();
    char path[FS_PATH_MAX];

    if (fs_arg(cmd, 1, path, sizeof path) <= 0) {
        snprintf(out, outlen, "usage: rm <file>\r\n");
        return pdFALSE;
    }
    snprintf(out, outlen, "rm %s: %s\r\n", path, (unlink(path) == 0) ? "OK" : "failed");
    return pdFALSE;
}

static BaseType_t cmd_rmdir(char *out, size_t outlen, const char *cmd)
{
    FS_GUARD_XFER();
    char path[FS_PATH_MAX];

    if (fs_arg(cmd, 1, path, sizeof path) <= 0) {
        snprintf(out, outlen, "usage: rmdir <dir>\r\n");
        return pdFALSE;
    }
    snprintf(out, outlen, "rmdir %s: %s\r\n", path, (rmdir(path) == 0) ? "OK" : "failed (missing/not empty)");
    return pdFALSE;
}

static BaseType_t cmd_mkdir(char *out, size_t outlen, const char *cmd)
{
    FS_GUARD_XFER();
    char path[FS_PATH_MAX];

    if (fs_arg(cmd, 1, path, sizeof path) <= 0) {
        snprintf(out, outlen, "usage: mkdir <dir>\r\n");
        return pdFALSE;
    }
    snprintf(out, outlen, "mkdir %s: %s\r\n", path, (mkdir(path, 0777) == 0) ? "OK" : "failed");
    return pdFALSE;
}

/* ----------------------------------------------------------------------- mv --- */
static BaseType_t cmd_mv(char *out, size_t outlen, const char *cmd)
{
    FS_GUARD_XFER();
    char a[FS_PATH_MAX], b[FS_PATH_MAX];

    if (fs_arg(cmd, 1, a, sizeof a) <= 0 || fs_arg(cmd, 2, b, sizeof b) <= 0) {
        snprintf(out, outlen, "usage: mv <old> <new>\r\n");
        return pdFALSE;
    }
    snprintf(out, outlen, "mv %s %s: %s\r\n", a, b, (rename(a, b) == 0) ? "OK" : "failed");
    return pdFALSE;
}

/* ----------------------------------------------------------------------- cp --- */
static BaseType_t cmd_cp(char *out, size_t outlen, const char *cmd)
{
    FS_GUARD_XFER();
    char a[FS_PATH_MAX], b[FS_PATH_MAX];
    int in, outfd, n, err = 0;
    long total = 0;

    if (fs_arg(cmd, 1, a, sizeof a) <= 0 || fs_arg(cmd, 2, b, sizeof b) <= 0) {
        snprintf(out, outlen, "usage: cp <src> <dst>\r\n");
        return pdFALSE;
    }
    in = open(a, O_RDONLY);
    if (in < 0) {
        snprintf(out, outlen, "cp: %s: not found\r\n", a);
        return pdFALSE;
    }
    outfd = open(b, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (outfd < 0) {
        close(in);
        snprintf(out, outlen, "cp: %s: cannot create\r\n", b);
        return pdFALSE;
    }
    while ((n = read(in, (char *)s_fsbuf, sizeof s_fsbuf)) > 0) {
        if (write(outfd, (char *)s_fsbuf, n) != n) {
            err = 1;
            break;
        }
        total += n;
    }
    close(in);
    close(outfd);
    snprintf(out, outlen, "cp %s -> %s: %ld bytes%s\r\n", a, b, total, err ? " (write error)" : "");
    return pdFALSE;
}

/* -------------------------------------------------------------------- touch --- */
static BaseType_t cmd_touch(char *out, size_t outlen, const char *cmd)
{
    FS_GUARD_XFER();
    char path[FS_PATH_MAX];
    int fd;

    if (fs_arg(cmd, 1, path, sizeof path) <= 0) {
        snprintf(out, outlen, "usage: touch <file>\r\n");
        return pdFALSE;
    }
    fd = open(path, O_WRONLY | O_CREAT, 0666);
    if (fd < 0) {
        snprintf(out, outlen, "touch %s: failed\r\n", path);
        return pdFALSE;
    }
    close(fd);
    snprintf(out, outlen, "touch %s: OK\r\n", path);
    return pdFALSE;
}

/* ----------------------------------------------------------------------- df --- */
static BaseType_t cmd_df(char *out, size_t outlen, const char *cmd)
{
    FS_GUARD_XFER();
    lfs_t *lfs;
    lfs_ssize_t used;
    unsigned long total_blk = LFS_CFG_BLOCK_COUNT;
    unsigned long bs = LFS_CFG_BLOCK_SIZE;
    unsigned long used_blk, free_blk;

    (void)cmd;
    lfs = vfs_lfs_mount();
    if (lfs == NULL) {
        snprintf(out, outlen, "df: no filesystem\r\n");
        return pdFALSE;
    }
    used = lfs_fs_size(lfs);
    if (used < 0) {
        snprintf(out, outlen, "df: lfs_fs_size error %d\r\n", (int)used);
        return pdFALSE;
    }
    used_blk = (unsigned long)used;
    free_blk = (used_blk < total_blk) ? (total_blk - used_blk) : 0;
    snprintf(out, outlen,
             "LittleFS: %lu/%lu blocks used  (%lu KB used, %lu KB free, %lu KB total, bs=%lu)\r\n",
             used_blk, total_blk,
             used_blk * bs / 1024UL, free_blk * bs / 1024UL, total_blk * bs / 1024UL, bs);
    return pdFALSE;
}

/* --------------------------------------------------------------------- mkfs --- */
static BaseType_t cmd_mkfs(char *out, size_t outlen, const char *cmd)
{
    FS_GUARD_XFER();
    char tok[8];
    int err;

    out[0] = '\0';
    if (fs_arg(cmd, 1, tok, sizeof tok) <= 0 ||
        (strcmp(tok, "yes") != 0 && strcmp(tok, "-y") != 0)) {
        snprintf(out, outlen,
                 "mkfs: ERASES the entire LittleFS partition (certs, env, all files).\r\n"
                 "      run 'mkfs yes' (or 'mkfs -y') to confirm.\r\n");
        return pdFALSE;
    }
    if (vfs_lfs_mount() == NULL) {       /* ensure mounted -> s_cfg geometry is built */
        snprintf(out, outlen, "mkfs: mount unavailable\r\n");
        return pdFALSE;
    }
    err = lfs_config_format();
    snprintf(out, outlen, "mkfs: format %s (err=%d)\r\n", (err == 0) ? "OK" : "FAILED", err);
    return pdFALSE;
}

/* -------------------------------------------------------------------- chmod --- */
static BaseType_t cmd_chmod(char *out, size_t outlen, const char *cmd)
{
    FS_GUARD_XFER();
    char marg[16], path[FS_PATH_MAX], *end;
    unsigned long v;
    uint16_t mode;
    lfs_t *lfs;
    int err;

    out[0] = '\0';
    if (fs_arg(cmd, 1, marg, sizeof marg) <= 0 || fs_arg(cmd, 2, path, sizeof path) <= 0) {
        snprintf(out, outlen, "usage: chmod <octal-mode> <path>\r\n");
        return pdFALSE;
    }
    v = strtoul(marg, &end, 8);          /* numeric octal only (like chmod's numeric form) */
    if (*end != '\0' || v > 07777) {
        snprintf(out, outlen, "chmod: invalid octal mode '%s'\r\n", marg);
        return pdFALSE;
    }
    lfs = vfs_lfs_mount();
    if (lfs == NULL) {
        snprintf(out, outlen, "chmod: no filesystem\r\n");
        return pdFALSE;
    }
    mode = (uint16_t)v;
    err = lfs_setattr(lfs, path, FS_ATTR_MODE, &mode, sizeof mode);
    if (err < 0) {
        snprintf(out, outlen, "chmod: %s: %s\r\n", path,
                 (err == LFS_ERR_NOENT) ? "no such file or directory" : "error");
        return pdFALSE;
    }
    snprintf(out, outlen, "chmod: %s -> %04lo\r\n", path, (unsigned long)mode);
    return pdFALSE;
}

#ifdef FS_CLI_HASH
/* ------------------------------------------------------ md5sum/sha*sum --- */
enum { H_MD5, H_SHA1, H_SHA224, H_SHA256, H_SHA384, H_SHA512 };

/* Stream <file> through the selected Mbed TLS hash (512-byte chunks via s_fsbuf, the
 * same read loop cat/hexdump use) and print "<hexdigest>  <path>" (coreutils style)
 * into the CLI buffer. The digest line fits the 2 KB out buffer (64 hex + path). */
static BaseType_t cmd_hash(char *out, size_t outlen, const char *cmd, int algo)
{
    FS_GUARD_XFER();
    char path[FS_PATH_MAX];
    unsigned char dig[64];
    union {
        mbedtls_md5_context    md5;
        mbedtls_sha1_context   sha1;
        mbedtls_sha256_context sha256;
        mbedtls_sha512_context sha512;
    } ctx;
    int fd, n, i, p, diglen;

    out[0] = '\0';
    if (fs_arg(cmd, 1, path, sizeof path) <= 0) {
        snprintf(out, outlen, "usage: <md5|sha1|sha224|sha256|sha384|sha512>sum <file>\r\n");
        return pdFALSE;
    }
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        snprintf(out, outlen, "%s: not found\r\n", path);
        return pdFALSE;
    }

    switch (algo) {
    case H_MD5:    mbedtls_md5_init(&ctx.md5);       mbedtls_md5_starts(&ctx.md5);          diglen = 16; break;
    case H_SHA1:   mbedtls_sha1_init(&ctx.sha1);     mbedtls_sha1_starts(&ctx.sha1);        diglen = 20; break;
    case H_SHA224: mbedtls_sha256_init(&ctx.sha256); mbedtls_sha256_starts(&ctx.sha256, 1); diglen = 28; break;
    case H_SHA256: mbedtls_sha256_init(&ctx.sha256); mbedtls_sha256_starts(&ctx.sha256, 0); diglen = 32; break;
    case H_SHA384: mbedtls_sha512_init(&ctx.sha512); mbedtls_sha512_starts(&ctx.sha512, 1); diglen = 48; break;
    default:       mbedtls_sha512_init(&ctx.sha512); mbedtls_sha512_starts(&ctx.sha512, 0); diglen = 64; break;
    }
    while ((n = read(fd, (char *)s_fsbuf, sizeof s_fsbuf)) > 0) {
        switch (algo) {
        case H_MD5:    mbedtls_md5_update(&ctx.md5, s_fsbuf, n);       break;
        case H_SHA1:   mbedtls_sha1_update(&ctx.sha1, s_fsbuf, n);     break;
        case H_SHA224:
        case H_SHA256: mbedtls_sha256_update(&ctx.sha256, s_fsbuf, n); break;
        default:       mbedtls_sha512_update(&ctx.sha512, s_fsbuf, n); break;
        }
    }
    close(fd);
    switch (algo) {
    case H_MD5:    mbedtls_md5_finish(&ctx.md5, dig);       mbedtls_md5_free(&ctx.md5);       break;
    case H_SHA1:   mbedtls_sha1_finish(&ctx.sha1, dig);     mbedtls_sha1_free(&ctx.sha1);     break;
    case H_SHA224:
    case H_SHA256: mbedtls_sha256_finish(&ctx.sha256, dig); mbedtls_sha256_free(&ctx.sha256); break;
    default:       mbedtls_sha512_finish(&ctx.sha512, dig); mbedtls_sha512_free(&ctx.sha512); break;
    }

    p = 0;
    for (i = 0; i < diglen; i++) {
        p += snprintf(out + p, outlen - p, "%02x", dig[i]);
    }
    snprintf(out + p, outlen - p, "  %s\r\n", path);
    return pdFALSE;
}

static BaseType_t cmd_md5sum   (char *o, size_t l, const char *c) { return cmd_hash(o, l, c, H_MD5);    }
static BaseType_t cmd_sha1sum  (char *o, size_t l, const char *c) { return cmd_hash(o, l, c, H_SHA1);   }
static BaseType_t cmd_sha224sum(char *o, size_t l, const char *c) { return cmd_hash(o, l, c, H_SHA224); }
static BaseType_t cmd_sha256sum(char *o, size_t l, const char *c) { return cmd_hash(o, l, c, H_SHA256); }
static BaseType_t cmd_sha384sum(char *o, size_t l, const char *c) { return cmd_hash(o, l, c, H_SHA384); }
static BaseType_t cmd_sha512sum(char *o, size_t l, const char *c) { return cmd_hash(o, l, c, H_SHA512); }
#endif /* FS_CLI_HASH */

/* ----------------------------------------------------------- registration --- */
void fs_cli_register(void)
{
    static const CLI_Command_Definition_t defs[] = {
        { "ls",      "ls [dir]:\r\n list a directory (default /)\r\n",                  cmd_ls,      -1 },
        /* -1 (variable param count): the path commands parse paths via fs_arg (quote-aware), so the
         * CLI must not reject a quoted "spaced file.txt" as an extra space-delimited parameter. The
         * handlers validate their own args. */
        { "cat",     "cat <file>:\r\n print a file to the console\r\n",                 cmd_cat,     -1 },
        { "hexdump", "hexdump <file> [max]:\r\n hex+ascii dump a file\r\n",             cmd_hexdump, -1 },
        { "rm",      "rm <file>:\r\n delete a file\r\n",                                cmd_rm,      -1 },
        { "rmdir",   "rmdir <dir>:\r\n remove an empty directory\r\n",                  cmd_rmdir,   -1 },
        { "mkdir",   "mkdir <dir>:\r\n create a directory\r\n",                         cmd_mkdir,   -1 },
        { "mv",      "mv <old> <new>:\r\n rename/move a file or directory\r\n",         cmd_mv,      -1 },
        { "cp",      "cp <src> <dst>:\r\n copy a file\r\n",                             cmd_cp,      -1 },
        { "touch",   "touch <file>:\r\n create an empty file if missing\r\n",          cmd_touch,   -1 },
        { "df",      "df:\r\n show LittleFS block usage\r\n",                           cmd_df,       0 },
        { "mkfs",    "mkfs [yes|-y]:\r\n format the LittleFS partition (ERASES all)\r\n", cmd_mkfs,  -1 },
        { "chmod",   "chmod <octal> <path>:\r\n set rwx permission bits (stored as an lfs attr)\r\n", cmd_chmod, -1 },
#ifdef FS_CLI_HASH
        { "md5sum",    "md5sum <file>:\r\n print the MD5 of a file\r\n",                 cmd_md5sum,   -1 },
        { "sha1sum",   "sha1sum <file>:\r\n print the SHA-1 of a file\r\n",              cmd_sha1sum,  -1 },
        { "sha224sum", "sha224sum <file>:\r\n print the SHA-224 of a file\r\n",          cmd_sha224sum,-1 },
        { "sha256sum", "sha256sum <file>:\r\n print the SHA-256 of a file\r\n",          cmd_sha256sum,-1 },
        { "sha384sum", "sha384sum <file>:\r\n print the SHA-384 of a file\r\n",          cmd_sha384sum,-1 },
        { "sha512sum", "sha512sum <file>:\r\n print the SHA-512 of a file\r\n",          cmd_sha512sum,-1 },
#endif
    };
    unsigned i;

    for (i = 0; i < sizeof(defs) / sizeof(defs[0]); i++) {
        FreeRTOS_CLIRegisterCommand(&defs[i]);
    }
}
