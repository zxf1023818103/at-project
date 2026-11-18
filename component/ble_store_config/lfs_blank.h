/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "lfs.h"
/* A mount failure is not permission to erase stored credentials. Scan the
 * complete region, including the last page, and propagate every read error. */
static inline int lfs_region_is_blank(const struct lfs_config *cfg)
{
    uint8_t page[256];
    if (!cfg || !cfg->read || !cfg->read_size || cfg->read_size>sizeof page ||
        sizeof page%cfg->read_size || cfg->block_size%sizeof page) return LFS_ERR_INVAL;
    for (lfs_block_t b=0;b<cfg->block_count;++b)
        for (lfs_off_t off=0;off<cfg->block_size;off+=sizeof page) {
            int rc=cfg->read(cfg,b,off,page,sizeof page);
            if (rc) return rc;
            for (unsigned i=0;i<sizeof page;++i) if (page[i]!=0xff) return 0;
        }
    return 1;
}
