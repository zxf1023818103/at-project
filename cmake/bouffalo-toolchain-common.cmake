# Shared tail for the Bouffalo chip toolchain files (bl602 / bl616 / bl616cl).
# The including file sets BOUFFALO_CHIP/MARCH/MABI/MTUNE, then includes this:
# it pulls in the RISC-V compiler and turns those values into -march/-mabi
# (+ -mtune when set; bl602 has none) on every language and the linker.

include(${CMAKE_CURRENT_LIST_DIR}/riscv64-unknown-elf.cmake)

set(_arch_flags "-march=${BOUFFALO_MARCH} -mabi=${BOUFFALO_MABI}")
if(BOUFFALO_MTUNE)
    string(APPEND _arch_flags " -mtune=${BOUFFALO_MTUNE}")
endif()

string(APPEND CMAKE_C_FLAGS_INIT          " ${_arch_flags}")
string(APPEND CMAKE_CXX_FLAGS_INIT        " ${_arch_flags}")
string(APPEND CMAKE_ASM_FLAGS_INIT        " ${_arch_flags}")
string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " ${_arch_flags}")

unset(_arch_flags)
