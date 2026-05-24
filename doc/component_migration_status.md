# Component-Migration Status (BL602)

记录 `experiment/driver-only` 当前的"已完成 / workaround / 未完成"状态，给下一段
上下文做 handoff。最近的整合 commit 是 `f5fb94c experiment: project/bouffalo on
chip-toolchain tree, BL602 builds e2e`。

## ✅ 已通

`make CHIP=bl602 BOARD=bl602dk` 在 [project/bouffalo/](../project/bouffalo/) 干净
end-to-end，产物：
- `build_bl602/at_bl602.elf` (~1.2 MB)
- `build_bl602/at_bl602_whole_flash_data.bin` (4 MB，包含 boot2 + 分区表 + 应用)
- `at_bl602.{bin,bin.hash,xz,xz.hash,xz.ota,bin.ota,map}`

构建走 chip-toolchain ([cmake/bl602-toolchain.cmake](../cmake/bl602-toolchain.cmake))
驱动，完全不依赖 `platform/bouffalo_sdk/project.build`。bin 生成走
`bflb_add_executable()` ([cmake/bflb_elf_to_bin.cmake](../cmake/bflb_elf_to_bin.cmake))
的 custom-command sibling target，没有 Makefile postbuild python 步骤。

## ⚠️ 已 workaround / 知道但没修

### ITCM 16K → 24K
[component/bouffalo_sdk/bl602_board_config/bl602_flash.ld](../component/bouffalo_sdk/bl602_board_config/bl602_flash.ld)
把 `itcm_memory` 从 16K bump 到 24K，从 DTCM 借了 8K（DTCM 之前只用 1 KB，富余）。

**根因：** legacy SDK 在链接行上用
`-Wl,--whole-archive ${SDK_LIBS} -Wl,--no-whole-archive` 把所有 SDK 静态库整组
强拉进 ELF；接着 `-Wl,--gc-sections + -flto` 在能看到完整候选集的情况下把不可达
段（包括各种 `.rodata`-with-function-pointer-tables）全部裁掉，最终 `.itcm_region`
只有 ~4 KB。

我们的 build 走 lazy linking + `--start-group`：LDS 的
`*bl602_*.o*(.rodata*)` 通配在每个被拉进来的 .o 上单独触发，匹配上来的 rodata
里嵌着函数指针表，让 gc-sections 把对应 `.text.tcm_code.*` 保活下来，雪球滚到
~24 KB。

**为什么没采用 legacy 路径：** 三层问题缠一起：
1. CMake 3.28 没有 `CMP0156`（链接行去重 policy，3.29+ 才落地）。我们的 INTERFACE
   伞型库 cascading 会把同一 `.a` 在 `LINK_LIBRARIES` 里塞 2–3 次，
   `--whole-archive` 下每次都会 force-include 整个 archive 的 `.vector`/`.init`
   段（LDS `KEEP` 的），第二次开始就 `cannot move location counter backwards`。
2. `$<LINK_LIBRARY:WHOLE_ARCHIVE,…>` + `LINK_LIBRARY_OVERRIDE` 实测会被 CMake 按
   feature 拆成多段 `--whole-archive`/`--no-whole-archive`，跨段重复仍然撞多
   定义。
3. `bflb_mbedtls_port/bignum.c` 跟 upstream `library/bignum.c` 完全重叠（56 vs 51
   个 `mbedtls_mpi_*`），SDK 是靠条件编译选其一；upstream 的 CMakeLists 无条件
   编译 bignum.c，没有干净的关闭开关。

留下的入口：
- [cmake/at_dedup_link_libs.cmake](../cmake/at_dedup_link_libs.cmake) — 递归
  walk `INTERFACE_LINK_LIBRARIES` 的 dedup helper，路径正确但目前没人调用。
- [cmake/bflb_elf_to_bin.cmake](../cmake/bflb_elf_to_bin.cmake) 里的 comment 标
  了重启入口（要 CMake 3.29+ 或者给 `cmake/mbedtls.cmake` 加一个 `ar -d
  bignum.c.obj` 的 post-build 把上游 bignum 剔掉，再把 `bflb_mbedtls_port/bignum.c`
  加回来）。

### `bignum.c` HW 加速被去掉
[component/bouffalo_sdk/bflb_mbedtls_port/CMakeLists.txt](../component/bouffalo_sdk/bflb_mbedtls_port/CMakeLists.txt)
把 `bignum.c` 从源列表里删了。代价：modular bignum（RSA / DH 路径上 `mbedtls_mpi_*`）
回落到上游软件实现；ECP 仍走 `bflb_mbedtls_hw_acc/ecp_alt.c` 的 PKA 路径不受影响。

为什么删：见上面 ITCM workaround 第 3 点。要找回来需要先解决"upstream
bignum 与 Bouffalo bignum 的二选一" —— 最干净的是 cmake/mbedtls.cmake 里在
mbedcrypto build 完之后 `ar d $<TARGET_FILE:mbedcrypto> bignum.c.obj`，然后把
bignum.c 加回 bflb_mbedtls_port。

### `bl_rand()` 用 `rand()` 兜底
[component/bouffalo_sdk/bflb_lhal/src/bl_rand_stub.c](../component/bouffalo_sdk/bflb_lhal/src/bl_rand_stub.c)
是 `__weak` 的 `rand()`-backed 实现。

为什么不用 `bl_port.c` 里 `#if defined(BL_MCU_SDK)` 那条 TRNG 路径：
`Sec_Eng_Trng_Get_Random` / `Sec_Eng_Trng_Enable` 在 `platform/bouffalo_sdk`
（当前 pin 的 `at-v2.3.26`）里**没有定义**——只在 `bl_port.c` 里 extern 声明，
源里没有实现，13 个预编译 `.a` 里也没有这俩符号。这是因为它们属于
`bouffalolab/bl_mcu_sdk` 这个**另一个** SDK；我们这边定义 `BL_MCU_SDK` 还会顺
带改 `bl_port.h` 的 include 布局（`<misc.h>` vs `<misc/util.h>`），整体不兼容。
详细分析见 [component/bouffalo_sdk/bflb_zypher_bluetooth/config/CMakeLists.txt](../component/bouffalo_sdk/bflb_zypher_bluetooth/config/CMakeLists.txt)
注释。

后续要接真 TRNG，可以直接 override `bl_rand`（weak 让位）；走 lhal 已有的
`bflb_sec_trng.c` 是最干净的方向。

### `CONFIG_SHELL=n`
[project/bouffalo/proj.conf](../project/bouffalo/proj.conf) 关了 shell。
`components/shell/` 还没抽，开启会让 `bflb_mm/mm_debug.c` 报 `shell.h: No such
file or directory`。Shell 抽过来后改回 `y` 即可。

### `bflb_zypher_bluetooth` profile-defs
[component/bouffalo_sdk/bflb_zypher_bluetooth/config/CMakeLists.txt](../component/bouffalo_sdk/bflb_zypher_bluetooth/config/CMakeLists.txt)
已经 wire 了三套 profile：
- bl602 + `m1s1`
- bl616 + `ble1m10s1bredr0`（10 conn / all roles / BLE-only）
- bl616cl + `m2s1`（2 conn / all roles / EM=16K）

BL616CL 还多一条 `-Wl,--defsym,__LD_CONFIG_EM_SIZE=16` 的 link option，因为
`bl616cl_common.ld.in` 读 `__LD_CONFIG_EM_SIZE` 决定 `__EM_SIZE`；BL616 的 ld
script 直接靠 `DEFINED(btble_controller_init) ? 32K : 0K` 自己判断，所以不需要
额外 defsym。

加新 profile 时：参考 [platform/bouffalo_sdk/components/wireless/bluetooth/ble_common_readonly.cmake](../platform/bouffalo_sdk/components/wireless/bluetooth/ble_common_readonly.cmake)
对应的 `_check_conflict` 块，在 config/CMakeLists.txt 里把 `CONFIG_BT_*` 一并 emit。`blestack/CMakeLists.txt` 里 BFLB_BT_* 选项默认值目前对三个芯片都成立，没有 chip-specific 分支。

## ❌ 还没干的、build 现在看不到但下一步会需要

### BL616 / BL616CL 整条 build 链
现在 [cmake/bl616-toolchain.cmake](../cmake/bl616-toolchain.cmake) 在；
`make CHIP=bl616 BOARD=bl616dk` 当前能 configure 干净、bouffalo_sdk 核心组件
（bflb_lhal / bl616_std / bflb_sysinit / freertos / lwipcore / mbedtls /
bflb_log / bflb_mm / bflb_sys / bflb_mbedtls_hw_acc / 等）也能编过 —— 这一步要
[component/bouffalo_sdk/bflb_lhal/CMakeLists.txt](../component/bouffalo_sdk/bflb_lhal/CMakeLists.txt)
里 derive `APP_VER_X/Y/Z` 三个宏（`bflb_common.c` 在 BL616/BL616CL/BL702L/BL618DG
路径下用 `app_ver` 结构体硬引用，BL602 路径上有 `#if defined(BL616)|...` 保护所以
没踩到；现在的逻辑是镜像 upstream `bouffalo_sdk/bsp/CMakeLists.txt` 从
`PROJECT_SDK_VERSION` 推出 x.y.z）。 但卡在：

- **`bflb_power_mgmt/pm_manager.c`** 找不到 `<lwip/tcpip.h>`、
  `<wifi_mgmr_ext.h>`、`<bl_lp.h>`。
  lwip include path 需要在 [component/bouffalo_sdk/bflb_power_mgmt/CMakeLists.txt](../component/bouffalo_sdk/bflb_power_mgmt/CMakeLists.txt)
  里 `target_link_libraries(... PUBLIC lwipcore)` 一行解掉。
  `wifi_mgmr_ext.h` 是 **wifi6 的** header（[platform/bouffalo_sdk/components/wireless/wifi6/fhost/include/wifi_mgmr_ext.h](../platform/bouffalo_sdk/components/wireless/wifi6/fhost/include/wifi_mgmr_ext.h)），
  下一项的衍生问题。

- **wifi6 / macsw 完全没抽**。[platform/bouffalo_sdk/components/wireless/wifi6/](../platform/bouffalo_sdk/components/wireless/wifi6/)
  26 MB，[platform/bouffalo_sdk/components/wireless/macsw/](../platform/bouffalo_sdk/components/wireless/macsw/)
  71 MB，五个 CMakeLists 入口：`wifi6/`, `wifi6/wpa_supplicant/`,
  `wifi6/wifi6_lwip_adapter/`, `wifi6/fhost_config/`, `wifi6/macsw_os_adapter/`。
  这是 BL616 build 链的真正瓶颈，按 wifi4 的抽法（已 done 的参考实现在
  [component/bouffalo_sdk/wifi4/](../component/bouffalo_sdk/wifi4/)）整一遍是
  下一段独立工作。`proj.conf` 里 BL616 / BL616CL 块设的
  `CONFIG_WIFI6 y` `CONFIG_FHOST y` `CONFIG_MACSW_SELECT default` 都跟这个相关。

- `bouffalo_sdk/CMakeLists.txt` 已经 conditionally `add_subdirectory(wifi6)`
  的入口还没添（参考 wifi4 的 `if(CONFIG_WIFI4 AND "${BOUFFALO_CHIP}" STREQUAL "bl602")`
  分支）。

复现命令：
```sh
cd project/bouffalo && rm -rf build_bl616 && make CHIP=bl616 BOARD=bl616dk
```
当前会停在 `bflb_power_mgmt/pm_manager.c:5: fatal error: lwip/tcpip.h: No such file or directory`。

### `bflb_ipc_rpmsg_ops`
[component/bouffalo_sdk/bflb_ipc_rpmsg_ops/CMakeLists.txt](../component/bouffalo_sdk/bflb_ipc_rpmsg_ops/CMakeLists.txt)
里的 comment 已经记了：依赖 OpenAMP + `bflb_multi_core_sync.h`，两者都没抽过来。
只在 BL618DG 双核才会用到，我们当前不支持 BL618DG。

### 一堆 `proj.conf` 选项没有对应组件
当前还设着但只是宏定义，没有 backing source：
- `CONFIG_BFLOG n` — 已关，没问题
- `CONFIG_TLSF y` — `bflb_mm` 里有 tlsf 子目录，被 lazy 拉，已工作
- `CONFIG_PING y` — `components/network/ping/` 没抽
- `CONFIG_BLSYNC y` — Bouffalo Lab Sync 没抽
- `CONFIG_COEX*` — Wi-Fi/BT 共存仲裁，编译时是宏；要不要抽 `components/wireless/coex/`
  待定
- `CONFIG_BLE_TP_SERVER y` — 已通过 `BFLB_BLE_TP_SERVER` 选项接上 blestack 的
  `services/ble_tp_svc.c`

确认要不要拉就视 BL602 实际跑起来后哪个 missing-symbol/funtionality 来决定。

### `src/main.cpp` 没改过
src/main.cpp 在 BL602 路径下直接 #include 一堆 Bouffalo Zephyr BT 头（
`bluetooth.h` / `l2cap.h` / `gatt.h` / `hci_core.h` / `hci_driver.h` /
`ble_lib_api.h`），现在能编只是因为 `bflb_zypher_bluetooth` 全部抽过来。
如果未来要做 BT-disabled build（比如 BL616 上某些 SKU），需要在 Common.h /
main.cpp 里加 `CONFIG_BLUETOOTH` 编译开关。

## 链接行去重 / WHOLE_ARCHIVE 重启清单

如果有人重新挑起 ITCM 对齐这件事，按这个顺序走：

1. **CMake 升到 3.29+** 后 `cmake_policy(SET CMP0156 NEW)` 可用；这会自动消掉
   cascading INTERFACE 重复。
2. 给 [cmake/mbedtls.cmake](../cmake/mbedtls.cmake) 加 mbedcrypto 的 post-build
   `ar d $<TARGET_FILE:mbedcrypto> bignum.c.obj`（带 chip-条件保护，避免在
   `CONFIG_MBEDTLS_BIGNUM_USE_HW=n` 时把上游 bignum 也删掉）。
3. 把 `bignum.c` 加回 [component/bouffalo_sdk/bflb_mbedtls_port/CMakeLists.txt](../component/bouffalo_sdk/bflb_mbedtls_port/CMakeLists.txt)。
4. 用 [cmake/at_dedup_link_libs.cmake](../cmake/at_dedup_link_libs.cmake)
   生成 deduped 列表，喂给 `$<LINK_LIBRARY:WHOLE_ARCHIVE,…>` —— CMP0156 NEW
   会让 CMake 不再把 cascading 重复也包成新的 `--whole-archive` 段。
5. 在 [cmake/riscv64-unknown-elf.cmake](../cmake/riscv64-unknown-elf.cmake) 重新
   注册 `CMAKE_<LANG>_LINK_LIBRARY_USING_WHOLE_ARCHIVE`（commit 里已经把这段
   删掉了，因为没用上；保留 git history 里就行）。
6. 验证 `.itcm_region` 回落到 ~4 KB，然后把 [bl602_flash.ld](../component/bouffalo_sdk/bl602_board_config/bl602_flash.ld)
   ITCM 改回 16K、DTCM 改回 48K。

## 验收用命令

```sh
cd project/bouffalo
make CHIP=bl602 BOARD=bl602dk        # ~30s 全量重建
ls -lh build_bl602/at_bl602*.bin
```

预期：
- `at_bl602_whole_flash_data.bin` 约 4.0 MB（含 boot2 + 分区表 + 应用 + lfs）
- `itcm_memory: 23956 B / 24 KB (97.48%)` 在内存使用报告里
