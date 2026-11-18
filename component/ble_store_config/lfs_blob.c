/* SPDX-License-Identifier: Apache-2.0 */
#include "lfs_blob.h"
#include <string.h>

int lfs_blob_read_exact(lfs_t *fs, const char *path, void *data, size_t size)
{
    uint8_t cache[LFS_BLOB_CACHE_MAX];
    struct lfs_file_config cfg = {.buffer = cache};
    lfs_file_t file;
    if (!fs || !fs->cfg || !path || (!data && size) || size > LFS_FILE_MAX || fs->cfg->cache_size > sizeof cache) return LFS_ERR_INVAL;
    int rc = lfs_file_opencfg(fs, &file, path, LFS_O_RDONLY, &cfg);
    if (rc) return rc;
    lfs_soff_t length = lfs_file_size(fs, &file);
    if (length < 0) rc = (int)length;
    else if ((size_t)length != size) rc = LFS_ERR_CORRUPT;
    else {
        lfs_ssize_t n = lfs_file_read(fs, &file, data, size);
        rc = n < 0 ? (int)n : (size_t)n == size ? 0 : LFS_ERR_CORRUPT;
    }
    int close = lfs_file_close(fs, &file);
    return rc ? rc : close;
}
int lfs_blob_replace(lfs_t *fs, const char *path, const char *tmp,
                    const void *data, size_t size)
{
    uint8_t cache[LFS_BLOB_CACHE_MAX];
    struct lfs_file_config cfg = {.buffer = cache};
    lfs_file_t file;
    if (!fs || !fs->cfg || !path || !tmp || (!data && size) || size > LFS_FILE_MAX || fs->cfg->cache_size > sizeof cache || !strcmp(path, tmp)) return LFS_ERR_INVAL;
    int rc = lfs_file_opencfg(fs, &file, tmp, LFS_O_WRONLY|LFS_O_CREAT|LFS_O_TRUNC, &cfg);
    if (rc) return rc;
    lfs_ssize_t n = lfs_file_write(fs, &file, data, size);
    if (n < 0) rc = (int)n;
    else if ((size_t)n != size) rc = LFS_ERR_IO;
    if (!rc) rc = lfs_file_sync(fs, &file);
    int close = lfs_file_close(fs, &file);
    if (!rc) rc = close;
    /* Interrupted writes leave the previous committed file intact. The next
     * transaction truncates the uncommitted temp; it is never loaded at boot. */
    if (!rc) rc = lfs_rename(fs, tmp, path);
    return rc;
}
