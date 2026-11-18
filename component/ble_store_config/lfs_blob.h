/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "lfs.h"

#ifndef LFS_BLOB_CACHE_MAX
#define LFS_BLOB_CACHE_MAX 512
#endif
/* Uses the platform's ALREADY mounted filesystem. No formatting, partition or
 * allocator policy. Serialize whole transactions per destination path; LFS's
 * per-operation lock alone does not serialize two writers to the same temp.
 * Temp files are never accepted as committed data after a reset. */
int lfs_blob_read_exact(lfs_t *fs, const char *path, void *data, size_t size);
int lfs_blob_replace(lfs_t *fs, const char *path, const char *temp,
                     const void *data, size_t size);

/* Same hook used by the shared VFS. Each platform owns a SINGLE mount. */
lfs_t *vfs_lfs_mount(void);
