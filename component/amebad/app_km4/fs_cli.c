/*
 * fs_cli.c — LittleFS file commands for the AmebaD KM4 FreeRTOS-Plus-CLI console:
 *   ls cat rm rmdir mkdir mv cp touch hexdump df mkfs
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
 * Output policy (matches the monitor's run_rom_cmd idiom): commands whose output can
 * exceed the 2 KB CLI buffer (ls/cat/hexdump) stream straight to the LOGUART via
 * DiagPutChar and leave the CLI buffer empty; short status lines use snprintf(out,...).
 * Direct writes also skip the per-line log timestamp, which is what a raw dump wants.
 */
#include <stdio.h>
#include <stdlib.h>          /* strtol */
#include <string.h>
#include <fcntl.h>           /* open, O_RDONLY/O_WRONLY/O_CREAT/O_TRUNC */
#include <unistd.h>          /* read, write, close, unlink, rmdir */
#include <sys/stat.h>        /* stat, mkdir */
#include <dirent.h>          /* opendir, readdir, closedir, DT_DIR (syscall_config's) */

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"
#include "ameba_soc.h"       /* DiagPutChar */

#include "lfs_config.h"      /* lfs_t, lfs_fs_size, LFS_CFG_BLOCK_*, lfs_config_format */
#include "syscalls.h"        /* vfs_lfs_mount */

#define FS_PATH_MAX   256

/* One scratch buffer shared by cat/cp/hexdump. Safe as a static: only the single
 * console (cli) task ever runs these handlers, one command at a time. */
static uint8_t s_fsbuf[512];

/* Copy CLI parameter `idx` (ptr+len, NOT NUL-terminated) into `buf`. Returns the
 * length, or -1 if the parameter is absent/empty. */
static int fs_arg(const char *cmd, UBaseType_t idx, char *buf, size_t sz)
{
    BaseType_t l = 0;
    const char *p = FreeRTOS_CLIGetParameter(cmd, idx, &l);

    if (p == NULL || l <= 0) {
        return -1;
    }
    if ((size_t)l >= sz) {
        l = (BaseType_t)(sz - 1);
    }
    memcpy(buf, p, (size_t)l);
    buf[l] = '\0';
    return (int)l;
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

/* ----------------------------------------------------------------------- ls --- */
static BaseType_t cmd_ls(char *out, size_t outlen, const char *cmd)
{
    char path[FS_PATH_MAX], child[FS_PATH_MAX], num[24];
    const char *dir;
    DIR *d;
    struct dirent *e;

    out[0] = '\0';
    dir = (fs_arg(cmd, 1, path, sizeof path) > 0) ? path : "/";

    d = opendir(dir);
    if (d == NULL) {
        snprintf(out, outlen, "ls: %s: cannot open\r\n", dir);
        return pdFALSE;
    }
    while ((e = readdir(d)) != NULL) {
        fs_emit(e->d_name);
        if (e->d_type == DT_DIR) {
            fs_emit("/\r\n");
        } else {
            struct stat st;
            join_path(child, sizeof child, dir, e->d_name);
            if (stat(child, &st) == 0) {
                snprintf(num, sizeof num, "  %ld\r\n", (long)st.st_size);
            } else {
                snprintf(num, sizeof num, "\r\n");
            }
            fs_emit(num);
        }
    }
    closedir(d);
    return pdFALSE;
}

/* ---------------------------------------------------------------------- cat --- */
static BaseType_t cmd_cat(char *out, size_t outlen, const char *cmd)
{
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

/* ----------------------------------------------------------- registration --- */
void fs_cli_register(void)
{
    static const CLI_Command_Definition_t defs[] = {
        { "ls",      "ls [dir]:\r\n list a directory (default /)\r\n",                  cmd_ls,      -1 },
        { "cat",     "cat <file>:\r\n print a file to the console\r\n",                 cmd_cat,      1 },
        { "hexdump", "hexdump <file> [max]:\r\n hex+ascii dump a file\r\n",             cmd_hexdump, -1 },
        { "rm",      "rm <file>:\r\n delete a file\r\n",                                cmd_rm,       1 },
        { "rmdir",   "rmdir <dir>:\r\n remove an empty directory\r\n",                  cmd_rmdir,    1 },
        { "mkdir",   "mkdir <dir>:\r\n create a directory\r\n",                         cmd_mkdir,    1 },
        { "mv",      "mv <old> <new>:\r\n rename/move a file or directory\r\n",         cmd_mv,       2 },
        { "cp",      "cp <src> <dst>:\r\n copy a file\r\n",                             cmd_cp,       2 },
        { "touch",   "touch <file>:\r\n create an empty file if missing\r\n",          cmd_touch,    1 },
        { "df",      "df:\r\n show LittleFS block usage\r\n",                           cmd_df,       0 },
        { "mkfs",    "mkfs [yes|-y]:\r\n format the LittleFS partition (ERASES all)\r\n", cmd_mkfs,  -1 },
    };
    unsigned i;

    for (i = 0; i < sizeof(defs) / sizeof(defs[0]); i++) {
        FreeRTOS_CLIRegisterCommand(&defs[i]);
    }
}
