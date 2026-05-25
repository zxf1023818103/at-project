# 任务目标

基于 CMake 搭建统一的构建系统。

# 概述

| 平台 | 构建系统 | 用户源码入口 |
|------|---------|------------|
| Ameba Dplus / Green2 | CMake | `src/CMakeLists.txt` + `ameba_add_internal_library(at_user)` |
| Ameba D | Makefile → CMake 子构建 | `src/CMakeLists.txt`（独立 CMake 工程） |
| BL602 / BL616 / BL616CL | CMake | `project/bouffalo/CMakeLists.txt` 直接 GLOB `src/*` |

# 共享源码清单

`src/CMakeLists.txt` 用一个 `file(GLOB CONFIGURE_DEPENDS …)` 把 `src/*.c` 和 `src/*.cpp` 收集到 `AT_USER_SOURCES`，并把 `src/` 自身路径暴露为 `AT_USER_SRC_DIR`。所有需要这份清单的 CMakeLists 都 `include()` 这个文件，避免在多个地方重复维护源码列表。

新增源文件直接放到 `src/` 顶层即可，无需改动构建脚本。子目录暂不支持（GLOB 非递归）。

# Ameba D

`project/amebad/Makefile` 入口未变，仍然递归 `make` 到 SDK 的 `project_hp` / `project_lp`。

用户代码不再走 SDK 原本的 `find $(CUSTOM_SRC_DIR) -name "*.c[pp]"` + `ar` 流程。
现在 `platform/ameba-rtos-d/project/realtek_amebaD_va0_example/GCC-RELEASE/project_hp/asdk/make/project/library/Makefile` 的 `lib:` 目标会：

1. 用 SDK 的 `CC` / `AR` / `GLOBAL_CFLAGS` / `CPPFLAGS` 生成一个 CMake init-cache (`build/amebad_user/init-cache.cmake`)。
2. 跑 `cmake -S src -B build/amebad_user -G "Unix Makefiles" -C init-cache.cmake` 配置独立工程。
3. 跑 `cmake --build build/amebad_user --parallel` 编出 `libat_user.a`。
4. `cp` 到 `$(ROOTDIR)/lib/application/lib_user.a`，下游 ELF 链接器仍然在原路径找到它。

入口 CMakeLists 是 `src/CMakeLists.txt`，独立 `project(at_user)`，直接 `add_library(at_user STATIC ${AT_USER_SOURCES})`。

# Ameba Dplus / Green2

`project/ameba/CMakeLists.txt` `include(src/CMakeLists.txt)` 后，调 `ameba_add_internal_library(at_user p_SOURCES ${AT_USER_SOURCES} …)` 注册到当前 image。

第三方 CMake 工程的标准接法（`ameba_add_subdirectory` + `ameba_port_standalone_internal_library`）在这里不适用：那条路径不把 SDK 的全局 include / define 注入到目标，会导致 `FreeRTOSConfig.h` 等系统头找不到。

参考 `platform/ameba-rtos/cmake/CMakeLists-template.cmake`。

# BL602 / BL616 / BL616CL

保持原状。`project/bouffalo/CMakeLists.txt` 仍然自己 GLOB `${SRC_DIR}/*.{c,cpp}` 然后 `target_sources(${proj_name}.elf PRIVATE …)`，不消费 `src/CMakeLists.txt`。原因是 bouffalo_sdk 的用户工程惯例就是直接把源文件挂到 elf target，引入共享 cmake 反而绕路；当前重复一次 GLOB 是可接受的成本。
