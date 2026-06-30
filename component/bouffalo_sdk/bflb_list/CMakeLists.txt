# bflb_list — generic singly/doubly-linked list helpers.
# Extracted from bouffalo_sdk/components/utils/list.
# Self-contained.

add_library(bflb_list STATIC utils_list.c)

target_include_directories(bflb_list PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
