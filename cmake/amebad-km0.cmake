# CMake toolchain file for AmebaD (RTL8721D) KM0 LP core.
#
# Usage:
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/amebad-km0.cmake
#
# KM0 is ARMv8-M baseline (Cortex-M23-class): no FPU, no DSP unit, no
# TrustZone-M CMSE. Mirrors GLOBAL_CFLAGS and LD_ARG in
# platform/ameba-rtos-d/.../project_lp/asdk/Makefile.include.gen (lines
# 243-272 and 293-310 respectively).

include(${CMAKE_CURRENT_LIST_DIR}/arm-none-eabi.cmake)

set(AMEBAD_CHIP  "amebad-km0"   CACHE STRING "AmebaD target core" FORCE)
set(AMEBAD_MARCH "armv8-m.base" CACHE STRING "ARM -march"         FORCE)

# Compile + link arch flags. Baseline profile -> no FPU/DSP/CMSE options.
set(_arch_flags "-march=${AMEBAD_MARCH} -mthumb")

# Chip-level defines that the SDK headers gate on. __ARM_FEATURE_DSP=0 is
# pinned to 0 explicitly in GLOBAL_CFLAGS because libgcc / CMSIS otherwise
# probe the DSP bit and assume yes on armv8-m.base, which fails to link.
set(_chip_defs "-DCONFIG_PLATFORM_8721D -D__ARM_FEATURE_DSP=0")

string(APPEND CMAKE_C_FLAGS_INIT          " ${_arch_flags} ${_chip_defs}")
string(APPEND CMAKE_CXX_FLAGS_INIT        " ${_arch_flags} ${_chip_defs}")
string(APPEND CMAKE_ASM_FLAGS_INIT        " ${_arch_flags} ${_chip_defs}")
string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " ${_arch_flags}")

unset(_arch_flags)
unset(_chip_defs)
