---
name: amebad-asdk-toolchain-path
description: AmebaD ARM toolchain (arm-none-eabi gcc/nm/ar/objdump) in-tree location for inspecting .a / building
metadata:
  type: reference
  originSessionId: 2c7314f2-7f7e-46e5-84f0-75d717be0da0
---

AmebaD 的 ARM 工具链(Realtek ASDK,arm-none-eabi binutils+gcc)在仓库内:
`toolchain/asdk-10.3.1-4354/linux/newlib/bin/`
(arm-none-eabi-{gcc,nm,ar,objdump,...};`arm-none-eabi-nm --version` =
GNU nm Realtek ASDK-10.3.1 Build 4354 / binutils 2.35.2)。

不在默认 PATH 里(WSL shell `which arm-none-eabi-nm` 找不到;`/usr/bin/nm` 是
x86 的,对 ARM .o 给空结果)。检查 AmebaD 预编译 `.a`(lib_pmc_*/lib_wifi_fw
等)的符号、或手工 objdump 时,直接用上面的绝对路径。RISC-V(博流)用的是另一套
`platform/toolchain_gcc_t-head_linux`,别混。
