# bflb_getopt — Bouffalo-flavored getopt for command-line parsing.
# Extracted from bouffalo_sdk/components/utils/getopt.
# Self-contained (only stdio/string).
#
# Re-added after the brief delete because the Zephyr BT stack
# (bflb_zypher_bluetooth_blestack/src/common/include/misc/util.h)
# pulls utils_getopt.h via the BOUFFALO_SDK branch.

add_library(bflb_getopt STATIC utils_getopt.c)

target_include_directories(bflb_getopt PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
