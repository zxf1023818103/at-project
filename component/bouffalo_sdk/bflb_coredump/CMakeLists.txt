# bflb_coredump — on-target crash dump encoder. Walks the stack, packs
# registers + memory regions into a binary blob, base64-frames it for
# transmission over UART. Extracted from
# bouffalo_sdk/components/utils/coredump.
#
# Two on-wire formats coexist; flip with BFLB_COREDUMP_V2:
#   v1 (default)  encoder_v1.c + bin_fmt_v1.c
#   v2            encoder_v2.c + bin_fmt_v2.c   (sets CONFIG_COREDUMP define)
#
# Backtrace support depends on components/debug/backtrace.h, which hasn't
# been extracted into component/ yet. Declared via INTERFACE expectation
# in the comment, not a target_link_libraries edge — when the debug
# component is ported, link it here.

option(BFLB_COREDUMP_V2 "Use the v2 dump format (encoder_v2 + bin_fmt_v2)" OFF)

add_library(bflb_coredump STATIC coredump.c)

if(BFLB_COREDUMP_V2)
    target_sources(bflb_coredump PRIVATE encoder_v2.c bin_fmt_v2.c)
    target_compile_definitions(bflb_coredump PUBLIC CONFIG_COREDUMP)
else()
    target_sources(bflb_coredump PRIVATE encoder_v1.c bin_fmt_v1.c)
endif()

target_include_directories(bflb_coredump PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Needs UART + flash from lhal, base64/CRC/hex from bflb_math.
target_link_libraries(bflb_coredump PUBLIC bflb_lhal bflb_math)
