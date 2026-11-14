/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "lfs.h"
#include "flash_layout.h"

typedef struct {
    lfs_t fs;
    struct lfs_config cfg;
    unsigned mounted, formatted;
    int error;
    uint8_t read_cache[TL_LFS_CACHE], prog_cache[TL_LFS_CACHE];
    uint32_t lookahead[2];
} TlLfs;

/* Called once before publishing the filesystem to the host task. Only an
 * entirely erased partition is initialized; other mount errors preserve it. */
int tl_lfs_mount(TlLfs *storage);
void tl_lfs_unmount(TlLfs *storage);

/* Synchronous physical flash primitives, supplied by the platform (or tests).
 * No XIP pointer arithmetic: the same filesystem is used from both banks. */
int tl_storage_read(uint32_t address, void *data, size_t length);
int tl_storage_prog(uint32_t address, const void *data, size_t length);
int tl_storage_erase(uint32_t address);
int tl_storage_lock(void);
int tl_storage_unlock(void);
void tl_storage_yield(void);

/* Platform-owned singleton, available after the first vfs_lfs_mount attempt. */
TlLfs *tl3238_lfs_state(void);
