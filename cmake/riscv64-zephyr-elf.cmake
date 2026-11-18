# Zephyr SDK RISC-V bare-metal cross toolchain (riscv64-zephyr-elf-*).
#
# Used by BL618DG **B0** silicon — the mass-production part, whose AP/NP cores
# are Nuclei-900 (rv32imafdc_zicsr_zifencei / ilp32d). The in-repo T-Head GCC
# 10.2 cannot build it: it rejects the `zifencei` extension
# ("Invalid or unknown z ISA extension").
#
# Install with: scripts/install_zephyr_riscv_toolchain.sh
# (lands in platform/toolchain_gcc_zephyr_linux/, gitignored — 1.1 GB, and
# upstream ships release tarballs only so it can't be a git submodule.)

set(_riscv_tool_prefix    "riscv64-zephyr-elf-")
set(_riscv_has_nano_specs FALSE)  # Zephyr SDK has no nano.specs (full newlib + picolibc)
set(_riscv_toolchain_stem "toolchain_gcc_zephyr_")

# This toolchain's C library is picolibc, not newlib. Consumers (component/syscall_config)
# use this to build the picolibc stdio bridge instead of relying on newlib's stdio.
set(AT_LIBC_PICOLIBC TRUE CACHE BOOL "Toolchain C library is picolibc" FORCE)

include(${CMAKE_CURRENT_LIST_DIR}/riscv-toolchain-common.cmake)

unset(_riscv_tool_prefix)
unset(_riscv_has_nano_specs)
unset(_riscv_toolchain_stem)
