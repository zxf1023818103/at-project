# bflb_mtd — minimal MTD-style flash partition wrapper + Boot2 helpers.
# Extracted from bouffalo_sdk/components/utils/bflb_mtd (the `include/` subdir
# is flattened — all .c/.h sit at the same level here).
#
# Depends on:
#   bflb_lhal      — bflb_core.h, bflb_flash.h
#   bflb_partition — partition.h

add_library(bflb_mtd STATIC
    bflb_mtd.c
    bflb_boot2.c
)

target_include_directories(bflb_mtd PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(bflb_mtd PUBLIC bflb_lhal bflb_partition)
