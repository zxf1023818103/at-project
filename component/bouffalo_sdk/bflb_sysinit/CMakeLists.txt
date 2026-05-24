# bflb_sysinit — provides sysinit_run_all(), which the chip startup
# (in bl<chip>_std) calls between System_Init() and main().
# Extracted from bouffalo_sdk/components/sysinit.
#
# Init callbacks register themselves into ".sysinit.<sub>.<ord>.<name>"
# sections from arbitrary downstream components via the SYSINIT() macro in
# sysinit.h. The linker script iterates those sections; sysinit_run_all
# walks them in order.

add_library(bflb_sysinit STATIC sysinit.c)

target_include_directories(bflb_sysinit PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
