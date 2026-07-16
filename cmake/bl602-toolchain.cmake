# CMake toolchain file for Bouffalo Lab BL602.
#
# Usage:
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/bl602-toolchain.cmake

set(BOUFFALO_CHIP  "bl602"     CACHE STRING "Bouffalo target chip" FORCE)
set(BOUFFALO_MARCH "rv32imafc" CACHE STRING "RISC-V -march"        FORCE)
set(BOUFFALO_MABI  "ilp32f"    CACHE STRING "RISC-V -mabi"         FORCE)
set(BOUFFALO_MTUNE ""          CACHE STRING "RISC-V -mtune"        FORCE)

include(${CMAKE_CURRENT_LIST_DIR}/bouffalo-toolchain-common.cmake)
