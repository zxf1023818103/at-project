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
# Toolchain: riscv64-zephyr-elf (Zephyr SDK GCC 14) — REQUIRED, not a preference.
# Bouffalo builds BL618DG's wireless libs with it AND with slim LTO: macsw,
# macsw_config and wl80211 contain ONLY GCC-14 LTO bytecode (.text is 0 bytes),
# so the in-repo T-Head GCC 10.2 cannot consume them at all
# ("bytecode stream ... generated with LTO version 14.0 instead of 9.0", and
# -fno-lto cannot help because there is no machine code to fall back to).
# Consequence: this toolchain's libc is picolibc, not newlib — see the bl618dg
# picolibc stdio bridge in component/syscall_config.
# Install: scripts/install_zephyr_riscv_toolchain.sh
#
# NOTE the SDK's b0 branch deliberately passes NO -mtune (only a0 does), so
# BOUFFALO_MTUNE stays empty here.

set(BOUFFALO_CHIP          "bl618dg"                   CACHE STRING "Bouffalo target chip" FORCE)
set(BOUFFALO_CPU_ID        "ap"                        CACHE STRING "BL618DG core (ap|np)" FORCE)
set(BOUFFALO_MARCH         "rv32imafdc_zicsr_zifencei" CACHE STRING "RISC-V -march"        FORCE)
set(BOUFFALO_MABI          "ilp32d"                    CACHE STRING "RISC-V -mabi"         FORCE)
set(BOUFFALO_MTUNE         ""                          CACHE STRING "RISC-V -mtune"        FORCE)
set(BOUFFALO_RISCV_TRIPLE  "riscv64-zephyr-elf"        CACHE STRING "RISC-V cross triple"  FORCE)

include(${CMAKE_CURRENT_LIST_DIR}/bouffalo-toolchain-common.cmake)
