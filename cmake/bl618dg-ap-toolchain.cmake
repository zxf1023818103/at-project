# CMake toolchain file for Bouffalo Lab BL618DG — AP core, B0 silicon.
#
# Usage:
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/bl618dg-ap-toolchain.cmake
#
# BL618DG is dual-core (AP + NP). Following the SDK's own solution_dualcore
# reference, the AP core runs the main application incl. the WiFi stack, and
# the NP core runs a minimal image — the same split as AmebaD's KM4 (main) /
# KM0 (minimal), so the two cores are separate CMake projects with their own
# toolchain file (mirrors cmake/amebad-km{0,4}.cmake).
#
# Silicon: B0 = the mass-production part (Bouffalo-confirmed), Nuclei-900 core:
# ilp32d, no xtheade, no P extension.
#
# Toolchain: riscv64-nuclei-elf (Nuclei GCC 14.2.1 + newlib). GCC **14** is a
# hard requirement, not a preference: Bouffalo builds BL618DG's wireless libs
# with slim LTO, so macsw, macsw_config and wl80211 contain ONLY GCC-14 LTO
# bytecode (.text is 0 bytes) and the in-repo T-Head GCC 10.2 cannot consume
# them at all ("bytecode stream ... generated with LTO version 14.0 instead of
# 9.0"; -fno-lto cannot help because there is no machine code to fall back to).
# Install: scripts/install_nuclei_riscv_toolchain.sh
#
# This replaces the Zephyr SDK, which met the GCC-14 bar but shipped picolibc —
# that needed a bridge TU in component/syscall_config for stdio, POSIX syscall
# naming and tp-register TLS. Nuclei's newlib matches every other platform here
# and brings nano.specs back.
#
# NOTE the SDK's b0 branch deliberately passes NO -mtune (only a0 does), so
# BOUFFALO_MTUNE stays empty here.

set(BOUFFALO_CHIP          "bl618dg"                   CACHE STRING "Bouffalo target chip" FORCE)
set(BOUFFALO_CPU_ID        "ap"                        CACHE STRING "BL618DG core (ap|np)" FORCE)
set(BOUFFALO_MARCH         "rv32imafdc_zicsr_zifencei" CACHE STRING "RISC-V -march"        FORCE)
set(BOUFFALO_MABI          "ilp32d"                    CACHE STRING "RISC-V -mabi"         FORCE)
set(BOUFFALO_MTUNE         ""                          CACHE STRING "RISC-V -mtune"        FORCE)
set(BOUFFALO_RISCV_TRIPLE  "riscv64-nuclei-elf"        CACHE STRING "RISC-V cross triple"  FORCE)

include(${CMAKE_CURRENT_LIST_DIR}/bouffalo-toolchain-common.cmake)
