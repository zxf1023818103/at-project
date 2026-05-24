# bflb_partition — partition-table reader (the SDK keeps a partition.toml
# generated into flash at a fixed address; this lib walks it at runtime).
# Extracted from bouffalo_sdk/components/utils/partition.
# Depends on bflb_lhal for bflb_core.h.

add_library(bflb_partition STATIC partition.c)

target_include_directories(bflb_partition PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(bflb_partition PUBLIC bflb_lhal)
