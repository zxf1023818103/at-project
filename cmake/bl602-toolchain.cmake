# CMake toolchain file for Bouffalo Lab BL602.
#
# Usage:
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/bl602-toolchain.cmake

include(${CMAKE_CURRENT_LIST_DIR}/riscv64-unknown-elf.cmake)

set(BOUFFALO_CHIP  "bl602"     CACHE STRING "Bouffalo target chip" FORCE)
set(BOUFFALO_MARCH "rv32imafc" CACHE STRING "RISC-V -march"        FORCE)
set(BOUFFALO_MABI  "ilp32f"    CACHE STRING "RISC-V -mabi"         FORCE)
set(BOUFFALO_MTUNE ""          CACHE STRING "RISC-V -mtune"        FORCE)

set(_arch_flags "-march=${BOUFFALO_MARCH} -mabi=${BOUFFALO_MABI}")

string(APPEND CMAKE_C_FLAGS_INIT          " ${_arch_flags}")
string(APPEND CMAKE_CXX_FLAGS_INIT        " ${_arch_flags}")
string(APPEND CMAKE_ASM_FLAGS_INIT        " ${_arch_flags}")
string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " ${_arch_flags}")

unset(_arch_flags)
