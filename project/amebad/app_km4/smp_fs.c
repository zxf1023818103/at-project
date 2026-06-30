/*
 * smp_fs.c - mcumgr fs_mgmt platform impl for AmebaD (RTL8721D).
 *
 * Maps the SMP file-system group (download / upload) onto the LittleFS-backed POSIX VFS
 * (component/syscall_config/newlib_syscalls.c: open/lseek/read/write/close -> lfs_file_*). Paths are
 * the raw LittleFS paths (e.g. "/config.txt"), the same ones the fs_cli console commands use.
 *
 * Each impl call is self-contained (open -> seek -> rw -> close): mcumgr drives the transfer chunk by
 * chunk with an explicit offset, so no file handle is held across calls.
 *
 * Built only for AMEBAD_ENABLE_MCUMGR. Registered from smp_img_ota_init().
 */
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#include "mgmt/mgmt.h"
#include "fs_mgmt/fs_mgmt_impl.h"
#include "at_log.h"

/* SMP clients (e.g. nRF Connect Device Manager) require a mountpoint and prepend it to every path
 * (default "lfs1" -> "/lfs1/test.txt"). Our LittleFS is a single root with no mountpoint, so strip a
 * leading "/lfs1" to get the real LFS path ("/test.txt"). Paths without the prefix pass through, so
 * the raw fs_cli paths still work. */
/* smp_img_ota.c: mark the SMP transfer window so console flash commands defer (see smp_xfer_active). */
extern void smp_xfer_touch(void);

#define FS_MOUNTPOINT   "/lfs1"

static const char *
strip_mount(const char *path)
{
    size_t n = strlen(FS_MOUNTPOINT);

    if (strncmp(path, FS_MOUNTPOINT, n) == 0 && (path[n] == '/' || path[n] == '\0')) {
        path += n;                          /* "/lfs1/test.txt" -> "/test.txt"; "/lfs1" -> "" */
        if (path[0] == '\0') {
            path = "/";
        }
    }
    return path;
}

int
fs_mgmt_impl_filelen(const char *path, size_t *out_len)
{
    int fd = open(strip_mount(path), O_RDONLY);
    off_t end;

    if (fd < 0) {
        return MGMT_ERR_ENOENT;
    }
    end = lseek(fd, 0, SEEK_END);
    close(fd);
    if (end < 0) {
        return MGMT_ERR_EUNKNOWN;
    }
    *out_len = (size_t) end;
    return 0;
}

int
fs_mgmt_impl_read(const char *path, size_t offset, size_t len,
                  void *out_data, size_t *out_len)
{
    int fd;
    ssize_t r;

    smp_xfer_touch();
    fd = open(strip_mount(path), O_RDONLY);

    if (fd < 0) {
        return MGMT_ERR_ENOENT;
    }
    if (lseek(fd, (off_t) offset, SEEK_SET) < 0) {
        close(fd);
        return MGMT_ERR_EUNKNOWN;
    }
    r = read(fd, out_data, len);
    close(fd);
    if (r < 0) {
        return MGMT_ERR_EUNKNOWN;
    }
    *out_len = (size_t) r;
    return 0;
}

int
fs_mgmt_impl_write(const char *path, size_t offset, const void *data, size_t len)
{
    /* offset 0 truncates (start of a new upload); later offsets extend the file. */
    int flags = (offset == 0) ? (O_WRONLY | O_CREAT | O_TRUNC) : (O_WRONLY | O_CREAT);
    int fd;
    ssize_t w;

    smp_xfer_touch();
    fd = open(strip_mount(path), flags);
    if (fd < 0) {
        log_error("[mcumgr] fs write open '%s' failed", strip_mount(path));
        return MGMT_ERR_EUNKNOWN;
    }
    if (offset != 0 && lseek(fd, (off_t) offset, SEEK_SET) < 0) {
        close(fd);
        return MGMT_ERR_EUNKNOWN;
    }
    w = write(fd, data, len);
    close(fd);
    if (w < (ssize_t) len) {
        return MGMT_ERR_EUNKNOWN;
    }
    return 0;
}
