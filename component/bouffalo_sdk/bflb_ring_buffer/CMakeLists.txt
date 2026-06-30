# bflb_ring_buffer — small lock-free ring buffer (single producer / single
# consumer). Extracted from bouffalo_sdk/components/utils/ring_buffer.
#
# test.c is a self-test main() that lived alongside the lib in the
# upstream tree; not part of the build.

add_library(bflb_ring_buffer STATIC ring_buffer.c)

target_include_directories(bflb_ring_buffer PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# ring_buffer.h pulls in bflb_core.h (compiler memory barriers + atomic
# load/store) from lhal.
target_link_libraries(bflb_ring_buffer PUBLIC bflb_lhal)
