# amebad_usrcfg_common — board / chip configuration data shared by both
# cores (boot config, IPC config, WiFi config). 3 .c. Built once per
# toolchain (KM0 build emits .o with armv8-m.base flags; KM4 build emits
# .o with armv8-m.main+dsp+cmse+fpv5 flags). Bootloader / image2 stages
# link the chip-specific version.
#
# Project-local copy of platform/ameba-rtos-d/.../fwlib/usrcfg/
# (chip-agnostic subset: filenames without "lp" / "hp" infix).
# Resync when the submodule pointer moves:
#   diff -r platform/ameba-rtos-d/component/soc/realtek/amebad/fwlib/usrcfg/ \
#           component/amebad/{usrcfg_common,usrcfg_lp,usrcfg_hp}/src/
#
# Not chip-gated — these .c only #include "ameba_soc.h" (no LP-specific
# RCC macros), so both KM0 and KM4 compile cleanly.

file(GLOB _srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
add_library(amebad_usrcfg_common STATIC ${_srcs})
target_link_libraries(amebad_usrcfg_common PUBLIC amebad_sdk_includes)

# OTA region — base addresses for the two image2 partitions. Default
# values match SDK upstream (rtl8721d_bootcfg.c literals = SDK partition
# table); override on the cmake line if relocating image2:
#   cmake -DAMEBAD_OTA1_ADDR=0x080xx000 -DAMEBAD_OTA2_ADDR=0x081xx000 ...
#
# Notes:
#   - 4 KB aligned (RSIP mask granularity).
#   - KM0 and KM4 image2 are combined into one blob; only KM0's start
#     address needs to be set (KM4 follows immediately after).
#   - Consumed by src/rtl8721d_bootcfg.c (see CMAKE_PATCH marker there).
#   - The matching SDK header values in rtl8721d_ota.h (LS_IMG2_OTA1_ADDR
#     / LS_IMG2_OTA2_ADDR) are only referenced by misc/rtl8721d_ota.c —
#     image2-stage OTA application code — and are NOT yet rewired here.
#     If you change these defaults, plan to also patch fwlib_headers/
#     rtl8721d_ota.h when image2 stage lands.
set(AMEBAD_OTA1_ADDR "0x08006000" CACHE STRING
    "AmebaD KM0 IMG2 OTA1 partition start address (4K-aligned)")
set(AMEBAD_OTA2_ADDR "0x08183000" CACHE STRING
    "AmebaD KM0 IMG2 OTA2 partition start address (4K-aligned)")
target_compile_definitions(amebad_usrcfg_common PRIVATE
    "AMEBAD_OTA1_ADDR=${AMEBAD_OTA1_ADDR}"
    "AMEBAD_OTA2_ADDR=${AMEBAD_OTA2_ADDR}"
)

target_compile_options(amebad_usrcfg_common PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
