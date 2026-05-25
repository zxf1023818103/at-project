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
# OTA region macros (LS_IMG2_OTA{1,2}_ADDR) are provided by
# amebad_sdk_includes — see its CMakeLists.txt for AMEBAD_OTA*_ADDR
# cmake options.

target_compile_options(amebad_usrcfg_common PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
