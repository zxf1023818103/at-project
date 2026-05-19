# Toolchain

各平台所用交叉编译器版本，以及项目和编译器各自指定 / 支持的 C / C++ 标准。所有 `__cplusplus` / `__STDC_VERSION__` 是直接 probe 编译器得到的。

## 概览

| 平台 | 架构 | GCC 版本 | 工具链 build | 项目强制 C | 项目强制 C++ | 默认 C | 默认 C++ | C++ 最高 |
|---|---|---|---|---|---|---|---|---|
| BL602 / BL616 / BL616CL | RV32 (T-Head) | 10.2.0 | Xuantie-900 V2.6.1 B-20220906 | `-std=gnu17` | `-std=gnu++17` | C17 | C++14 | C++20 |
| Ameba D (RTL8721D / RTL8710C) | ARMv7E-M | 10.3.1 | Realtek ASDK 10.3.1 Build 4354 | — | — | C17 | C++14 | C++20 |
| Ameba Dplus (RTL8711Dx) | ARMv8-M | 10.3.1 | Realtek ASDK 10.3.1 Build 4523 | — | `-std=c++11` | C17 | C++14 | C++20 |
| Ameba Green2 (RTL8711F) | ARMv8-M | 12.3.1 | Realtek ASDK 12.3.1 Build 4568 | — | `-std=c++11` | C17 | C++17 | C++23 |

"项目强制"指 SDK / 项目 CMake / Makefile 给所有 TU 显式追加的 `-std=` 标志。"默认 C/C++" 是不带 `-std=` 时编译器自身的默认值。

## Bouffalo 系（BL602 / BL616 / BL616CL）

T-Head 提供的 RISC-V newlib 工具链，作为 git submodule 放在 `platform/toolchain_gcc_t-head_linux/`，构建时 `Makefile` 会把 `bin/` 加进 PATH。

- 编译器：[platform/toolchain_gcc_t-head_linux/bin/riscv64-unknown-elf-gcc](platform/toolchain_gcc_t-head_linux/bin/riscv64-unknown-elf-gcc) / `riscv64-unknown-elf-g++`
- `gcc --version`: `Xuantie-900 elf newlib gcc Toolchain V2.6.1 B-20220906) 10.2.0`
- 项目全局标志（[platform/bouffalo_sdk/cmake/compiler_flags.cmake:39](platform/bouffalo_sdk/cmake/compiler_flags.cmake#L39)）：
  - `$<$<COMPILE_LANGUAGE:C>:-std=gnu17>`
  - `$<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>`

## Ameba D（RTL8721D / RTL8710C）

老款 Ameba D SDK 自带工具链，跟 SDK 一起 vendored 在 submodule 里（KM0 LP / KM4 HP 各放一份，本质同一套二进制）。

- 编译器：`platform/ameba-rtos-d/project/realtek_amebaD_va0_example/GCC-RELEASE/project_{hp,lp}/toolchain/linux/asdk-10.3.1/linux/newlib/bin/arm-none-eabi-{gcc,g++}`
- `gcc --version`: `arm-none-eabi-gcc (Realtek ASDK-10.3.1 Build 4354) 10.3.1 20220117`
- SDK 没有给所有 TU 强制 `-std=`，`src/main.cpp` 走编译器默认（C++14）

## Ameba Dplus（RTL8711Dx）和 Green2（RTL8711F）

新版 ameba-rtos 把工具链拆出 SDK，第一次 `source env.sh` 时按 SoC 自动下载到 `/opt/rtk-toolchain/`（或 `$RTK_TOOLCHAIN_DIR`）。两个 SoC 用不同 ASDK 版本，所以也是不同 GCC：

- Dplus → `/opt/rtk-toolchain/asdk-10.3.1-4523/linux/newlib/bin/arm-none-eabi-{gcc,g++}`
  - `arm-none-eabi-gcc (Realtek ASDK-10.3.1 Build 4523) 10.3.1 20220117`
  - 由 [platform/ameba-rtos/cmake/toolchain/ameba-toolchain-asdk-10.3.1.cmake](platform/ameba-rtos/cmake/toolchain/ameba-toolchain-asdk-10.3.1.cmake) 选用
- Green2 → `/opt/rtk-toolchain/asdk-12.3.1-4568/linux/newlib/bin/arm-none-eabi-{gcc,g++}`
  - `arm-none-eabi-gcc (Realtek ASDK-12.3.1 Build 4568) 12.3.1 20251113`
  - 由 [platform/ameba-rtos/cmake/toolchain/ameba-toolchain-asdk-12.3.1.cmake](platform/ameba-rtos/cmake/toolchain/ameba-toolchain-asdk-12.3.1.cmake) 选用

两个 SoC 共用同一份 SDK CMake，C++ 全局强制 `-std=c++11`（[platform/ameba-rtos/cmake/flags/common/compile_options.cmake:61](platform/ameba-rtos/cmake/flags/common/compile_options.cmake#L61)），所以即使 Green2 编译器默认 C++17，实际编译还是按 C++11 规则走。C 没有显式 `-std=`，跟编译器默认（C17）。

## 注意事项

- Ameba 三个平台 C++ 都被 SDK 限到 ≤ C++11（D 因走默认而落到 C++14，仍远低于 Bouffalo 的 C++17）。`src/main.cpp` 共享一份代码，写跨平台代码时按 **C++11** 的语法可用面取最小公分母。
- Green2 工具链是 GCC 12.3，**比** Dplus / D 的 10.3 / Bouffalo 的 10.2 都新。诊断信息和警告会更严，迁移代码到 Green2 时可能见到老编译器没报过的 `-Wxxx`。
- `/opt/rtk-toolchain/` 下的两套 ASDK 是 `source project/ameba/env.sh` 第一次跑时按需下载的，跟 SDK 仓库无强绑定 —— 换机器要重做一次。
