# bflb_tree — header-only BSD-style sys/tree.h port (red-black + splay).
# Extracted from bouffalo_sdk/components/utils/tree. INTERFACE-only,
# no compiled sources.

add_library(bflb_tree INTERFACE)

target_include_directories(bflb_tree INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
