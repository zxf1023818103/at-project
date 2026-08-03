# CMake toolchain file for Bouffalo Lab BL618DG — NP core, B0 silicon.
#
# Usage:
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/bl618dg-np-toolchain.cmake
#
# The NP core runs the minimal image (AmebaD KM0's role). Same ISA/ABI as the
# AP core on B0 silicon — the cores differ in the SDK by CPU_ID (IRQ count,
# linker script, which peripherals the board layer brings up), not by -march.
# See bl618dg-ap-toolchain.cmake for the silicon/toolchain rationale.

set(BOUFFALO_CHIP          "bl618dg"                   CACHE STRING "Bouffalo target chip" FORCE)
set(BOUFFALO_CPU_ID        "np"                        CACHE STRING "BL618DG core (ap|np)" FORCE)
set(BOUFFALO_MARCH         "rv32imafdc_zicsr_zifencei" CACHE STRING "RISC-V -march"        FORCE)
set(BOUFFALO_MABI          "ilp32d"                    CACHE STRING "RISC-V -mabi"         FORCE)
set(BOUFFALO_MTUNE         ""                          CACHE STRING "RISC-V -mtune"        FORCE)
set(BOUFFALO_RISCV_TRIPLE  "riscv64-nuclei-elf"        CACHE STRING "RISC-V cross triple"  FORCE)

include(${CMAKE_CURRENT_LIST_DIR}/bouffalo-toolchain-common.cmake)
