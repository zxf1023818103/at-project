# CMake toolchain file for AmebaD (RTL8721D) KM4 HP core.
#
# Usage:
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/amebad-km4.cmake
#
# KM4 is ARMv8-M main profile (Cortex-M33-class): hardware FPU
# (fpv5-sp-d16), DSP extension, TrustZone-M (CMSE). Mirrors GLOBAL_CFLAGS
# / LD_ARG in platform/ameba-rtos-d/.../project_hp/asdk/Makefile.include.gen
# (lines 380-420 and 463-487 respectively).

set(AMEBAD_CHIP  "amebad-km4"        CACHE STRING "AmebaD target core" FORCE)
set(AMEBAD_MARCH "armv8-m.main+dsp"  CACHE STRING "ARM -march"         FORCE)

# Compile + link arch flags. Main profile -> FPU + CMSE + hard float.
set(_arch_flags "-march=${AMEBAD_MARCH} -mthumb -mcmse -mfloat-abi=hard -mfpu=fpv5-sp-d16")

# __FPU_PRESENT is the CMSIS-standard knob FPU code-paths gate on.
set(_chip_defs "-D__FPU_PRESENT")

include(${CMAKE_CURRENT_LIST_DIR}/amebad-toolchain-common.cmake)
