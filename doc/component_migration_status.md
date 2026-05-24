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

### ~~ITCM 16K → 24K → 32K~~ → **回到 16K 默认（CONFIG_ROMAPI y）**
[project/bouffalo/proj.conf](../project/bouffalo/proj.conf) 的 BL602 块加了
`set(CONFIG_ROMAPI y)`。
[component/bouffalo_sdk/bl602_std/CMakeLists.txt](../component/bouffalo_sdk/bl602_std/CMakeLists.txt)
看到这个 flag 就：
1. 把 `src/bl602_romapi.c` 加进 source 列表
2. 给整个 lib 加 `BFLB_USE_ROM_DRIVER` 编译宏

`bl602_aon.c` / `bl602_common.c` / `bl602_ef_ctrl.c` / `bl602_sf_cfg_ext.c` /
`bl602_sflash_ext.c` / `bl602_xip_sflash_ext.c` 里所有 `ATTR_TCM_SECTION`
函数都被 `#ifndef BFLB_USE_ROM_DRIVER` 包着 —— 一旦定义这个宏，整段 full C
实现被 ifdef 出去，留下的是 `bl602_romapi.c` 里 `__ALWAYS_INLINE` 的 10-byte
thunk，做的事情就是 `(BL_Err_Type(*)(void))ROM_APITABLE[index]()`。BL602
BootROM 在 `0x21010800` 维护了一张 API jump table，所有 PDS / XIP / flash /
EF / SF-cfg 的 HAL routine 实际驻留 ROM，不占 ITCM。

**实测效果：**
| 区域 | 修前 | 修后 |
|---|---|---|
| itcm_memory | 23956 B / 24 KB (97.48%) | **7924 B / 16 KB (48.36%)** |
| dtcm_memory | 1028 B / 40 KB | 1028 B / 48 KB (默认) |
| xip_memory | 779252 B / 1 MB | 778748 B / 1 MB |

`bl602_flash.ld` ITCM/DTCM 已经还原到 upstream 默认（16K / 48K）。

**走过的弯路（留给以后避坑）：**
1. 先怀疑 LDS 里 `*bl602_*.o*(.rodata*)` 通配让 rodata 函数指针表保活
   `*(.tcm_code.*)` → 抓 map 验证那十几条 wildcard 在我们 build 里**匹配字节
   数都是 0**，假设不成立。
2. 再怀疑没上 LTO 导致 chain endpoint 丢不掉 → 上 LTO（compile 加 `-flto=auto`
   + AR 换 `gcc-ar`），ITCM 只从 23956 → 23860（-96 B），证明 LTO 也不是关键。
3. 跑 upstream `examples/wifi/sta/wifi_tcp` 例子量到 ITCM 3884 B / 16 KB
   (23.71%)，对比 map 看到 upstream 里 `AON_LowPower_Enter_PDS0` 等是从
   `bl602_romapi.c.obj` 的 0xa 字节 thunk 来的，我们这边是从 `bl602_aon.c.obj`
   的 0xa8 字节完整实现来的 —— 锁定差异来源 BFLB_USE_ROM_DRIVER 这个 define。
   `bl602_aon.c` 里所有函数都用 `#ifndef BFLB_USE_ROM_DRIVER` 包着；upstream
   `project.build:110` 默认 `CONFIG_ROMAPI ?= y` 自动启用，我们 proj.conf 没
   设过这一条。
4. 一并把 LTO 关了（compile 端 `-flto=auto` + `gcc-ar` 那两行 revert 掉）。
   失去 ITCM 这个诱因之后，LTO 剩 0.91% 的 xip 收益（同样 16K ITCM + ROMAPI
   下 LTO on/off 实测 xip 771692 → 778748 = -7056 B），不值 build-time 增加
   + 多套 ar wrapper 维护成本。LTO 要重启的话，已经 verify 过 `-flto=auto` +
   `riscv64-unknown-elf-gcc-ar/gcc-ranlib` 这组就能跑通。

**遗留 dedup / WHOLE_ARCHIVE 工作（跟 ITCM 没关，但场子还在）：**
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

留下的入口（如果以后真要做 whole-archive 一致性）：
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

## 验收用命令

```sh
cd project/bouffalo
make CHIP=bl602 BOARD=bl602dk        # ~30s 全量重建
ls -lh build_bl602/at_bl602*.bin
```

预期（CONFIG_ROMAPI y / LTO off / ITCM 16K / DTCM 48K — 全部 upstream 默认）：
- `at_bl602_whole_flash_data.bin` 约 4.0 MB（含 boot2 + 分区表 + 应用 + lfs）
- `xip_memory: 778748 B / 1 MB (74.27%)`
- `itcm_memory: 7924 B / 16 KB (48.36%)`
- `dtcm_memory: 1028 B / 48 KB (2.09%)`
- `ram_wifi: 35824 B / 40 KB (87.46%)`

参考 upstream `examples/wifi/sta/wifi_tcp` 例子（同样 wifi4 + lwip + freertos
工作量）跑出来：
- `xip_memory: 465648 B / 1 MB (44.41%)` — 比我们小是因为没 BT stack + mbedtls +
  cjson + zlib + lfs；
- `itcm_memory: 3884 B / 16 KB (23.71%)` — ROMAPI 同样开，差距主要在我们多链了
  bflb_uart/bflb_gpio/bflb_mtimer/bflb_irq/bflb_sflash/bflb_sf_ctrl 一些没被
  romapi cover 的 TCM-marked 函数。
