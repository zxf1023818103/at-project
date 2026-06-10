#include "lfs_config.h"

void lfs_config_fill(struct lfs_config *cfg)
{
    cfg->read_size      = LFS_CFG_READ_SIZE;
    cfg->prog_size      = LFS_CFG_PROG_SIZE;
    cfg->block_size     = LFS_CFG_BLOCK_SIZE;
    cfg->block_count    = LFS_CFG_BLOCK_COUNT;
    cfg->cache_size     = LFS_CFG_CACHE_SIZE;
    cfg->lookahead_size = LFS_CFG_LOOKAHEAD_SIZE;
    cfg->block_cycles   = LFS_CFG_BLOCK_CYCLES;
}
