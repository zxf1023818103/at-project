# bflb_block_pool — fixed-size block allocator utility.
# Extracted from bouffalo_sdk/components/utils/bflb_block_pool.
# Self-contained (only stdint/stdlib).

add_library(bflb_block_pool STATIC bflb_block_pool.c)

target_include_directories(bflb_block_pool PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
