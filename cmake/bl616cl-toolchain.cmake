# CMake toolchain file for Bouffalo Lab BL616CL.
#
# Usage:
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/bl616cl-toolchain.cmake

include(${CMAKE_CURRENT_LIST_DIR}/riscv64-unknown-elf.cmake)

set(BOUFFALO_CHIP  "bl616cl"            CACHE STRING "Bouffalo target chip" FORCE)
set(BOUFFALO_MARCH "rv32imafc_xtheade"  CACHE STRING "RISC-V -march"        FORCE)
set(BOUFFALO_MABI  "ilp32f"             CACHE STRING "RISC-V -mabi"         FORCE)
set(BOUFFALO_MTUNE "e907"               CACHE STRING "RISC-V -mtune"        FORCE)

set(_arch_flags "-march=${BOUFFALO_MARCH} -mabi=${BOUFFALO_MABI} -mtune=${BOUFFALO_MTUNE}")

string(APPEND CMAKE_C_FLAGS_INIT          " ${_arch_flags}")
string(APPEND CMAKE_CXX_FLAGS_INIT        " ${_arch_flags}")
string(APPEND CMAKE_ASM_FLAGS_INIT        " ${_arch_flags}")
string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " ${_arch_flags}")

unset(_arch_flags)
