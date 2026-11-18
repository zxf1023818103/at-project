# T-Head Xuantie RISC-V bare-metal cross toolchain (riscv64-unknown-elf-*).
#
# Chip-specific toolchain files (bl602-toolchain.cmake, bl616-toolchain.cmake,
# bl616cl-toolchain.cmake, bl618dg-{ap,np}-toolchain.cmake for A-series silicon)
# include this file and then append -march/-mabi/-mtune via
# CMAKE_<LANG>_FLAGS_INIT, so a single project tree can target multiple
# Bouffalo SoCs by swapping CMAKE_TOOLCHAIN_FILE.
#
# BL618DG **B0** (mass-production, Nuclei-900) uses riscv64-zephyr-elf.cmake
# instead — this GCC 10.2 rejects that core's `zifencei` extension.

set(_riscv_tool_prefix    "riscv64-unknown-elf-")
set(_riscv_has_nano_specs TRUE)   # T-Head GCC ships newlib-nano
set(_riscv_toolchain_stem "toolchain_gcc_t-head_")

include(${CMAKE_CURRENT_LIST_DIR}/riscv-toolchain-common.cmake)

unset(_riscv_tool_prefix)
unset(_riscv_has_nano_specs)
unset(_riscv_toolchain_stem)
