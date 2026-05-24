# bflb_timestamp — small monotonic-counter / time helper utility.
# Extracted from bouffalo_sdk/components/utils/bflb_timestamp.
# Self-contained (only stdint).

add_library(bflb_timestamp STATIC bflb_timestamp.c)

target_include_directories(bflb_timestamp PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
