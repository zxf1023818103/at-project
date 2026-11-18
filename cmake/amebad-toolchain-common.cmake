# Shared tail for the AmebaD (RTL8721D) per-core toolchain files (km0 / km4).
# The including file sets AMEBAD_CHIP plus _arch_flags / _chip_defs, then
# includes this: it pulls in the ARM compiler and applies the flags on every
# language and the linker. CONFIG_PLATFORM_8721D is common to both cores.

include(${CMAKE_CURRENT_LIST_DIR}/arm-none-eabi.cmake)

set(_chip_defs "-DCONFIG_PLATFORM_8721D ${_chip_defs}")

string(APPEND CMAKE_C_FLAGS_INIT          " ${_arch_flags} ${_chip_defs}")
string(APPEND CMAKE_CXX_FLAGS_INIT        " ${_arch_flags} ${_chip_defs}")
string(APPEND CMAKE_ASM_FLAGS_INIT        " ${_arch_flags} ${_chip_defs}")
string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " ${_arch_flags}")

unset(_arch_flags)
unset(_chip_defs)
