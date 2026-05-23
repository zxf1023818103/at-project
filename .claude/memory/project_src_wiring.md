---
name: project-src-wiring
description: "How at-project/src is pulled into each platform's firmware build (Bouffalo / Ameba new / AmebaD)"
metadata: 
  node_type: memory
  type: project
  originSessionId: 42f3082b-8468-40e9-8522-e5e8ab5f779c
---

at-project/src/ 下所有 `.cpp/.c` 会被以下三个构建路径自动收集，**新增源文件不需要手动登记**：

- **`project/ameba/`**（RTL8711Dx / RTL8711F）：`project/ameba/CMakeLists.txt` 用 `file(GLOB CONFIGURE_DEPENDS "${SRC_DIR}/*.cpp" "${SRC_DIR}/*.c")` 收集（非递归，仅 src 顶层）。
- **`project/amebad/`**（RTL8721D / RTL8710C）：通过 SDK 内的 `platform/ameba-rtos-d/project/realtek_amebaD_va0_example/GCC-RELEASE/project_hp/asdk/make/project/library/Makefile`，里面 `CUSTOM_SRC_DIR = $(BASEDIR)/../../src` + `find $(CUSTOM_SRC_DIR) -name "*.cpp"`（递归）打成 `lib_user.a`。**只有 project_hp（KM4）走这条路；project_lp（KM0）不拉 at-project/src。**
- **`project/bouffalo/`**：`project/bouffalo/CMakeLists.txt` 显式 list 或 glob src（按需查证）。

**Why:** 之前一次规划误以为 amebad 不拉 at-project/src（grep 没匹中是因为它用 `find` 不是 `wildcard`/`GLOB`），结果计划里把 amebad 划成 out-of-scope，被用户纠正。

**How to apply:** 给 src/ 加新 `.cpp` 时，无需改三个 project 的构建脚本；但要确保新文件用 `#ifdef CONFIG_PLATFORM_*` / `BL602|BL616|...` 围起来，让非目标平台编译时为空 TU，避免在不该编的平台引入未解析符号。
