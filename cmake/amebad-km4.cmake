# CMake toolchain file for AmebaD (RTL8721D) KM4 HP core.
#
# Usage:
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/amebad-km4.cmake
#
# KM4 is ARMv8-M main profile (Cortex-M33-class): hardware FPU
# (fpv5-sp-d16), DSP extension, TrustZone-M (CMSE). Mirrors GLOBAL_CFLAGS
# / LD_ARG in platform/ameba-rtos-d/.../project_hp/asdk/Makefile.include.gen
# (lines 380-420 and 463-487 respectively).

include(${CMAKE_CURRENT_LIST_DIR}/arm-none-eabi.cmake)

set(AMEBAD_CHIP  "amebad-km4"        CACHE STRING "AmebaD target core" FORCE)
set(AMEBAD_MARCH "armv8-m.main+dsp"  CACHE STRING "ARM -march"         FORCE)

# Compile + link arch flags. Main profile -> FPU + CMSE + hard float.
set(_arch_flags
    "-march=${AMEBAD_MARCH}"
    "-mthumb"
    "-mcmse"
    "-mfloat-abi=hard"
    "-mfpu=fpv5-sp-d16"
)
list(JOIN _arch_flags " " _arch_flags_str)

# Chip-level defines (SDK GLOBAL_CFLAGS). __FPU_PRESENT is the CMSIS-
# standard knob FPU code-paths gate on.
set(_chip_defs "-DCONFIG_PLATFORM_8721D -D__FPU_PRESENT")

string(APPEND CMAKE_C_FLAGS_INIT          " ${_arch_flags_str} ${_chip_defs}")
string(APPEND CMAKE_CXX_FLAGS_INIT        " ${_arch_flags_str} ${_chip_defs}")
string(APPEND CMAKE_ASM_FLAGS_INIT        " ${_arch_flags_str} ${_chip_defs}")
string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " ${_arch_flags_str}")

unset(_arch_flags)
unset(_arch_flags_str)
unset(_chip_defs)
