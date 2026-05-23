# 嵌入式 C++ 依赖库可行性 checklist

针对 at-project 这套环境（arm-none-eabi-gcc 12.3.1 + Realtek/Bouffalo SDK + newlib + FreeRTOS）选 C++ 第三方库时的速查清单。每条都是真实碰过的坑。

## 实测结果

| 库 | 版本 | 三平台编译 | Green2 实测 | 备注 |
|---|---|---|---|---|
| madler/zlib | v1.3.1 | ✅ AmebaD/Green2/Dplus/BL616 全过 | ✅ roundtrip 127B→27B | 干净的 C 库, 全平台兼容 |
| zlib-ng | 2.2.5 | ❌ | — | 见下文「zlib-ng 不兼容」 |
| abseil-cpp | LTS 20250127.0 | ❌ | — | 见下文「abseil 不兼容」 |

## 选库前的兼容性 checklist

挑 C/C++ 第三方库前用这张表过一遍，命中任何一条都得提前规划解决方案，不然花一整天才发现走不通。

### 1. 标准库假设

- [ ] **不能依赖 `<mutex>` / `<thread>` / `<condition_variable>` / `<future>`**
  - newlib for arm-none-eabi 的 libstdc++ **没带 pthread 支持**, 整个 `<mutex>` header 里 `std::mutex` 这个类都不存在
  - 影响: abseil cctz 用 `std::mutex` → 全爆
  - 自检: `echo '#include <mutex>' | arm-none-eabi-g++ -std=gnu++17 -xc++ - -c -o /dev/null` 编不过

- [ ] **不能依赖 `<filesystem>`**
  - 同上, libstdc++ filesystem 依赖 POSIX FS

- [ ] **可以用**: `<vector> <string> <map> <unordered_map> <algorithm> <memory> <utility> <type_traits> <optional> <variant> <tuple> <chrono>` 等纯 header-only 部分

### 2. POSIX 系统接口假设

- [ ] **不能依赖 `pthread_*`, `sched_yield`, `nanosleep`, `clock_gettime` 直接可用**
  - 这些函数 newlib 头里**默认不声明**, 要 `_POSIX_THREADS / _POSIX_TIMERS / _POSIX_PRIORITY_SCHEDULING` 宏才暴露
  - Realtek SDK 有 `freertos_posix` 组件做 FreeRTOS+POSIX 桥接, 但不是默认链接的, 需要 Kconfig 开
  - 影响: abseil base 层全依赖 pthread + spinlock

- [ ] **不能依赖 Linux ELF runtime introspection**
  - `<link.h>` (用来读 `dl_iterate_phdr`) 在 bare-metal 不存在
  - 影响: abseil debugging / leak detection / symbolize 全用这个

- [ ] **不能依赖 GNU libc 的 struct tm 扩展**
  - newlib 的 `struct tm` 没有 `tm_gmtoff` / `tm_zone` / `__tm_gmtoff` / `__tm_zone`
  - 影响: 所有自己做时区计算的 C++ 库 (abseil cctz, Howard Hinnant date 等)

### 3. CMake 配置层

- [ ] **`target_compile_features(... cxx_std_17)` 不一定能用**
  - 在这个交叉构建里 CMake 把编译器 ID 报成纯 `"GNU"` 没附带版本号, 没匹配到 feature DB, target_compile_features 直接抛 "no known features for CXX compiler GNU"
  - 自检: abseil 等用 `target_compile_features` 强制 C++ 标准的库会爆
  - workaround: 用 `set(CMAKE_CXX_STANDARD 17)` 替代

- [ ] **`-std=gnu++17` 是 target-level 不是 project-level**
  - Ameba/Bouffalo SDK 通过 `target_compile_options` 给自己的 target 加 std, 不设 CMAKE_CXX_STANDARD
  - 第三方库子项目的 `check_cxx_source_compiles` / try_compile 会用 toolchain 默认的 C++ 标准 (gcc 12 默认 gnu++17, 旧版可能是 gnu++14), 检测结果跟实际编译不一致
  - workaround: 在 add_subdirectory 之前 `set(CMAKE_CXX_STANDARD 17)`

- [ ] **`CMAKE_TRY_COMPILE_TARGET_TYPE` 必须设成 STATIC_LIBRARY**
  - cross toolchain 加 `-nostartfiles + custom linker script`, 默认的 try_compile (link 一个 exe) 必失败 → 所有 `check_function_exists / check_c_source_compiles` 跟着失败
  - workaround: `set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")` 放在 add_subdirectory 之前

- [ ] **第三方库默认开的 BUILD_SHARED_LIBS 要关**
  - 嵌入式没动态加载, 必须 `set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)`

- [ ] **测试 / examples 要关**
  - `BUILD_TESTING=OFF`, `ZLIB_BUILD_EXAMPLES=OFF`, `ABSL_BUILD_TESTING=OFF` 等, 否则会试图链接 exe 失败

### 4. Toolchain 类型别名陷阱

- [ ] **`int32_t != int`, `size_t != unsigned long` 在 arm-none-eabi-gcc 12 上是真实情况**
  - `__INT32_TYPE__ = long int`, `__SIZE_TYPE__ = unsigned int`
  - 32 位宽度相同, 但 C/C++ 当作不同类型
  - 影响: zlib-ng 头声明用 `int32_t` / 实现用 `int` 时 "conflicting types" 编译失败
  - 自检: `echo '' | arm-none-eabi-gcc -E -dM -mcpu=cortex-m33 -x c - | grep INT32_TYPE`

### 5. SDK strict warning stack

- [ ] **第三方库源码要单独 `-w`**
  - Ameba SDK 全局 `-Wall -Wextra -Wundef -Werror`, zlib 这种历史代码 (`#if BIG_ENDIAN` / 隐式有符号比较) 必然炸
  - workaround: `target_compile_options(<lib_target> PRIVATE -w)` 只对第三方 target 关警告, SDK 自己源码保持严格

## 具体踩坑案例

### zlib-ng (失败)

- 头声明 `int32_t zng_compress2(...)` vs 实现 `int Z_EXPORT PREFIX(compress2)(...)`
- arm-none-eabi 上 `int != int32_t` → 全 API "conflicting types"
- compat 模式 (`ZLIB_COMPAT=ON`) 一样炸
- 同样问题在 native 模式 (`size_t` vs `unsigned int`) 又一遍
- 结论: zlib-ng 在非主流 toolchain 上要打多处 vendor patch 才能用, 不如直接用 madler/zlib

### abseil (失败)

- abseil base 层 `pthread_key_create / pthread_setspecific / sched_yield / nanosleep` 全依赖 POSIX
- cctz 用 `std::mutex` (libstdc++ 没带) 和 GNU `tm` 扩展 (newlib 没有)
- debugging 用 `<link.h>` (bare-metal 没有)
- 不是几行 CMake 配置能补的, abseil 设计目标就是 server-class POSIX
- 要在这个环境跑起来要么完整接 freertos_posix + 重编 libstdc++, 要么手工 patch 砍掉 cctz/debug/threading

### madler/zlib (成功 ✓)

- 纯 C 标准库 + 标准 stdio, 不碰 pthread / filesystem / 时区
- 标准库假设最弱
- Green2 实测 127B→27B (21% 压缩比) roundtrip OK

## 建议的「先验证一下能不能用」流程

新引一个第三方库前:

1. 看库 README 是否声称支持 baremetal / FreeRTOS / "no POSIX"
2. `grep -r "pthread_\|std::mutex\|std::thread\|<link.h>\|tm_gmtoff" 库src/` 看是否命中第 1-2 节里的雷区
3. 看 CMakeLists 是否用 `target_compile_features`, 是否 `find_package(Threads)` 是否要求 `Threads::Threads`
4. 用本仓的 src/zlib.cmake 作模板写 lib.cmake, 然后小测试 link 验证
5. 命中任何一条要事先估工时, 不要直接 `add_subdirectory` 完就以为完事

## 推荐的 C++ 兼容库 (待验证)

如果只是要验证 C++ 17 工具链兼容性、不想深陷 POSIX 依赖, 推荐:

- **fmtlib/fmt** — header-only `{fmt}` printf 替代, 无 POSIX 依赖
- **nlohmann/json** — header-only JSON, 纯 C++17
- **catchorg/Catch2** v2 (header-only) — 单元测试
- **chriskohlhoff/asio** standalone header-only 模式 — 不开 SSL 不开 IO_URING 就行
- **CLIUtils/CLI11** — header-only 命令行解析

这些都没有 abseil / boost 那种 "server-class POSIX assumed" 的设计前提。
