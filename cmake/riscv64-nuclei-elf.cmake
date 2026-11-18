# Nuclei RISC-V bare-metal cross toolchain (GCC 14.2.1 + newlib).
#
# Used by BL618DG **B0** silicon — the mass-production part, whose AP/NP cores
# are Nuclei-900 (rv32imafdc_zicsr_zifencei / ilp32d). Two hard requirements it
# satisfies, and one thing it fixes:
#
#   * GCC 14. The vendor's macsw / wl80211 libs for bl618dg are slim-LTO
#     archives — GCC-14 LTO bytecode with a zero-byte .text — so the toolchain
#     must be the same GCC generation to read them at all. (This is why the
#     in-repo T-Head GCC 10.2 is not an option; it also rejects `zifencei`.)
#   * The multilib set carries rv32imafdc_zaamo_zalrsc/ilp32d, which is exactly
#     what those archives declare in Tag_RISCV_arch.
#   * Its C library is **newlib**, not picolibc. That is the difference from the
#     Zephyr SDK this replaces: picolibc needed a bridge TU in
#     component/syscall_config (stdio FILE objects, un-prefixed POSIX syscall
#     names, and TLS set up off the RISC-V tp register before main). On newlib
#     bl618dg uses the same C library as every other platform in this repo and
#     none of that is needed. It also ships nano.specs, so bl618dg gets the same
#     newlib-nano size trimming as bl602 / bl616 / bl616cl.
#
# Install with: scripts/install_nuclei_riscv_toolchain.sh
# (lands in toolchain/nuclei_riscv_newlibc_<version>/, gitignored — 3.7 GB
# extracted, and upstream ships release tarballs only so it can't be a submodule.)
#
# NOTE: the binary prefix is riscv64-unknown-elf-, the SAME as the T-Head GCC.
# The two are told apart purely by directory, so BOUFFALO_TOOLCHAIN_ROOT (or the
# _riscv_toolchain_reldir below) must always win over a PATH lookup — never rely
# on PATH order to pick between them.

set(_riscv_tool_prefix      "riscv64-unknown-elf-")
set(_riscv_has_nano_specs   TRUE)
set(_riscv_toolchain_reldir "toolchain/nuclei_riscv_newlibc_2025.10")

include(${CMAKE_CURRENT_LIST_DIR}/riscv-toolchain-common.cmake)

unset(_riscv_tool_prefix)
unset(_riscv_has_nano_specs)
unset(_riscv_toolchain_reldir)
