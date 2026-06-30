/*
 * zip_cli.c — minizip (zlib contrib/minizip) ZIP archive console commands for the AmebaD KM4
 * FreeRTOS-Plus-CLI: `zip` (create) and `unzip` (list / extract), over the LittleFS VFS.
 *
 * DEMO feature: built only into the non-QUIC, non-AWS console images (see app_km4 CMakeLists).
 * minizip's working set (~50 KB unzip / tunable zip) is comfortable on the lighter images'
 * 200 KB+ free heap, but risky on the heap-tight AWS image. Registered from main() like the
 * other *_cli.c command files.
 *
 * All file I/O routes through a custom zlib_filefunc_def ioapi backed by the POSIX VFS
 * (open/read/write/lseek/close -> LittleFS), so no stdio / fopen is pulled in. `zip` uses a small
 * deflate window (windowBits=-14 raw, memLevel=6 ~ 96 KB) since LittleFS files are tiny.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"
#include "ameba_soc.h"      /* DiagPutChar */

#include "zip.h"            /* minizip create (pulls in zlib.h + ioapi.h) */
#include "unzip.h"          /* minizip read */

#define ZIP_PATH_MAX   256

/* One I/O chunk buffer shared by zip/unzip (single console task -> sequential). The deflate/
 * inflate state is allocated internally by zlib, not here. */
static uint8_t s_zbuf[2048];

/* ---- POSIX-VFS ioapi (no stdio) ------------------------------------------ *
 * minizip's default ioapi uses fopen/fread/...; this one binds the zlib_filefunc_def to the
 * VFS syscalls so archives live on LittleFS without dragging in newlib stdio. */
static voidpf zvfs_open(voidpf opaque, const char *filename, int mode)
{
    int flags, fd;
    ( void ) opaque;
    if ((mode & ZLIB_FILEFUNC_MODE_READWRITEFILTER) == ZLIB_FILEFUNC_MODE_READ)
        flags = O_RDONLY;
    else if (mode & ZLIB_FILEFUNC_MODE_CREATE)
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (mode & ZLIB_FILEFUNC_MODE_EXISTING)
        flags = O_RDWR;
    else
        return NULL;
    fd = open(filename, flags, 0666);
    /* VFS file fds are >= 3, so a valid fd casts to a non-NULL handle (NULL == open error). */
    return (fd < 0) ? NULL : (voidpf)(intptr_t)fd;
}
static uLong zvfs_read(voidpf opaque, voidpf stream, void *buf, uLong size)
{
    int n;
    ( void ) opaque;
    n = read((int)(intptr_t)stream, buf, (size_t)size);
    return (n < 0) ? 0 : (uLong)n;
}
static uLong zvfs_write(voidpf opaque, voidpf stream, const void *buf, uLong size)
{
    int n;
    ( void ) opaque;
    n = write((int)(intptr_t)stream, (void *)buf, (size_t)size);
    return (n < 0) ? 0 : (uLong)n;
}
static long zvfs_tell(voidpf opaque, voidpf stream)
{
    ( void ) opaque;
    return (long)lseek((int)(intptr_t)stream, 0, SEEK_CUR);
}
static long zvfs_seek(voidpf opaque, voidpf stream, uLong offset, int origin)
{
    int whence;
    ( void ) opaque;
    whence = (origin == ZLIB_FILEFUNC_SEEK_CUR) ? SEEK_CUR
           : (origin == ZLIB_FILEFUNC_SEEK_END) ? SEEK_END : SEEK_SET;
    return (lseek((int)(intptr_t)stream, (long)offset, whence) < 0) ? -1 : 0;
}
static int zvfs_close(voidpf opaque, voidpf stream)
{
    ( void ) opaque;
    return close((int)(intptr_t)stream);
}
static int zvfs_error(voidpf opaque, voidpf stream)
{
    ( void ) opaque;
    ( void ) stream;
    return 0;
}
static void zvfs_fill(zlib_filefunc_def *p)
{
    p->zopen_file  = zvfs_open;
    p->zread_file  = zvfs_read;
    p->zwrite_file = zvfs_write;
    p->ztell_file  = zvfs_tell;
    p->zseek_file  = zvfs_seek;
    p->zclose_file = zvfs_close;
    p->zerror_file = zvfs_error;
    p->opaque      = NULL;
}

/* ---- helpers ------------------------------------------------------------- */
static int zip_arg(const char *cmd, UBaseType_t idx, char *buf, size_t sz)
{
    BaseType_t l = 0;
    const char *p = FreeRTOS_CLIGetParameter(cmd, idx, &l);

    if (p == NULL || l <= 0) return -1;
    if ((size_t)l >= sz) l = (BaseType_t)(sz - 1);
    memcpy(buf, p, (size_t)l);
    buf[l] = '\0';
    return (int)l;
}

static void zip_emit(const char *s)
{
    while (*s) DiagPutChar(*s++);
}

/* Create each parent directory of `path` (mkdir per '/' component; ignores already-exists). */
static void zip_makedirs(const char *path)
{
    char tmp[ZIP_PATH_MAX + 2];
    size_t i;

    strncpy(tmp, path, sizeof tmp - 1);
    tmp[sizeof tmp - 1] = '\0';
    for (i = 1; tmp[i] != '\0'; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            mkdir(tmp, 0777);
            tmp[i] = '/';
        }
    }
}

/* ---------------------------------------------------------------- zip ----- */
static BaseType_t cmd_zip(char *out, size_t outlen, const char *cmd)
{
    char arc[ZIP_PATH_MAX], src[ZIP_PATH_MAX];
    zlib_filefunc_def io;
    zipFile zf;
    UBaseType_t ai;
    int added = 0, skipped = 0;

    out[0] = '\0';
    if (zip_arg(cmd, 1, arc, sizeof arc) <= 0 || zip_arg(cmd, 2, src, sizeof src) <= 0) {
        snprintf(out, outlen, "usage: zip <archive.zip> <file>...\r\n");
        return pdFALSE;
    }
    zvfs_fill(&io);
    zf = zipOpen2(arc, APPEND_STATUS_CREATE, NULL, &io);
    if (zf == NULL) {
        snprintf(out, outlen, "zip: cannot create %s\r\n", arc);
        return pdFALSE;
    }
    for (ai = 2; zip_arg(cmd, ai, src, sizeof src) > 0; ai++) {
        const char *ename = (src[0] == '/') ? src + 1 : src;   /* zip entries are relative */
        int fd = open(src, O_RDONLY);
        int n;

        if (fd < 0) { skipped++; continue; }
        /* Small deflate: windowBits=-14 (raw, 16 KB window) + memLevel=6 -> ~96 KB, plenty for
         * tiny LittleFS files and safe on the lighter images' heap. inflate (unzip) auto-handles
         * the smaller window. */
        if (zipOpenNewFileInZip3(zf, ename, NULL, NULL, 0, NULL, 0, NULL,
                                 Z_DEFLATED, Z_DEFAULT_COMPRESSION, 0,
                                 -14, 6, Z_DEFAULT_STRATEGY, NULL, 0) != ZIP_OK) {
            close(fd); skipped++; continue;
        }
        while ((n = read(fd, s_zbuf, sizeof s_zbuf)) > 0)
            zipWriteInFileInZip(zf, s_zbuf, (unsigned)n);
        close(fd);
        zipCloseFileInZip(zf);
        added++;
    }
    zipClose(zf, NULL);
    snprintf(out, outlen, "zip %s: %d added%s\r\n", arc, added,
             skipped ? ", some skipped (missing)" : "");
    return pdFALSE;
}

/* -------------------------------------------------------------- unzip ----- */
static BaseType_t cmd_unzip(char *out, size_t outlen, const char *cmd)
{
    char a1[ZIP_PATH_MAX], arc[ZIP_PATH_MAX], name[ZIP_PATH_MAX], dest[ZIP_PATH_MAX + 2];
    char line[ZIP_PATH_MAX + 64];
    zlib_filefunc_def io;
    unzFile uf;
    UBaseType_t argi = 1;
    int list_only = 0, count = 0, extracted = 0, rc;

    out[0] = '\0';
    if (zip_arg(cmd, 1, a1, sizeof a1) > 0 && strcmp(a1, "-l") == 0) { list_only = 1; argi = 2; }
    if (zip_arg(cmd, argi, arc, sizeof arc) <= 0) {
        snprintf(out, outlen, "usage: unzip [-l] <archive.zip>\r\n");
        return pdFALSE;
    }
    zvfs_fill(&io);
    uf = unzOpen2(arc, &io);
    if (uf == NULL) {
        snprintf(out, outlen, "unzip: cannot open %s\r\n", arc);
        return pdFALSE;
    }
    rc = unzGoToFirstFile(uf);
    while (rc == UNZ_OK) {
        unz_file_info info;
        size_t nl;

        if (unzGetCurrentFileInfo(uf, &info, name, sizeof name, NULL, 0, NULL, 0) != UNZ_OK)
            break;
        count++;
        nl = strlen(name);
        if (list_only) {
            snprintf(line, sizeof line, "  %-28s %lu -> %lu  crc %08lx\r\n", name,
                     (unsigned long)info.uncompressed_size, (unsigned long)info.compressed_size,
                     (unsigned long)info.crc);
            zip_emit(line);
        } else {
            snprintf(dest, sizeof dest, "/%s", name);
            zip_makedirs(dest);                        /* create parent dirs (+ dir entries) */
            if (nl > 0 && name[nl - 1] != '/') {       /* a file -> extract */
                if (unzOpenCurrentFile(uf) == UNZ_OK) {
                    int ofd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                    if (ofd >= 0) {
                        int n;
                        while ((n = unzReadCurrentFile(uf, s_zbuf, sizeof s_zbuf)) > 0)
                            write(ofd, s_zbuf, n);
                        close(ofd);
                        extracted++;
                    }
                    unzCloseCurrentFile(uf);           /* also verifies the entry CRC */
                }
            }
        }
        rc = unzGoToNextFile(uf);
    }
    unzClose(uf);
    if (list_only)
        snprintf(out, outlen, "unzip -l %s: %d entr%s\r\n", arc, count, (count == 1) ? "y" : "ies");
    else
        snprintf(out, outlen, "unzip %s: %d/%d extracted\r\n", arc, extracted, count);
    return pdFALSE;
}

/* ----------------------------------------------------------- registration --- */
void zip_cli_register(void)
{
    static const CLI_Command_Definition_t defs[] = {
        { "zip",   "zip <archive.zip> <file>...:\r\n create a ZIP from LittleFS files (minizip/deflate)\r\n", cmd_zip,   -1 },
        { "unzip", "unzip [-l] <archive.zip>:\r\n list (-l) or extract a ZIP to LittleFS (minizip)\r\n",      cmd_unzip, -1 },
    };
    unsigned i;

    for (i = 0; i < sizeof(defs) / sizeof(defs[0]); i++) {
        FreeRTOS_CLIRegisterCommand(&defs[i]);
    }
}
