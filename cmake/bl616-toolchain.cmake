# CMake toolchain file for Bouffalo Lab BL616 / BL618.
#
# Usage:
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/bl616-toolchain.cmake

set(BOUFFALO_CHIP  "bl616"                          CACHE STRING "Bouffalo target chip" FORCE)
set(BOUFFALO_MARCH "rv32imafcpzpsfoperand_xtheade"  CACHE STRING "RISC-V -march"        FORCE)
set(BOUFFALO_MABI  "ilp32f"                         CACHE STRING "RISC-V -mabi"         FORCE)
set(BOUFFALO_MTUNE "e907"                           CACHE STRING "RISC-V -mtune"        FORCE)

include(${CMAKE_CURRENT_LIST_DIR}/bouffalo-toolchain-common.cmake)
