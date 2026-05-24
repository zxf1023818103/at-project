# Component-Migration Status

记录 `experiment/driver-only` 当前的"已完成 / workaround / 未完成"状态，给下一段
上下文做 handoff。

**Branch head**: `3c8c342 project/bouffalo: use component/CMakeLists.txt's auto-enumeration`（往前数 7 个 commit 都是这一段会话做的；`f60760f` 之前是更早期 BL602 e2e 完工时的 baseline）。

## ✅ 已通

**三平台 e2e build** 在 [project/bouffalo/](../project/bouffalo/) 都干净：

| 平台 | 命令 | 产物 |
|---|---|---|
| bl602 / bl602dk | `make CHIP=bl602 BOARD=bl602dk` | `build_bl602/at_bl602_whole_flash_data.bin` (4 MB) |
| bl616 / bl616dk | `make CHIP=bl616 BOARD=bl616dk` | `build_bl616/at_bl616.elf` |
| bl616cl / bl616cldk | `make CHIP=bl616cl BOARD=bl616cldk` | `build_bl616cl/at_bl616cl.elf` |

构建走 chip-toolchain（[cmake/bl602-toolchain.cmake](../cmake/bl602-toolchain.cmake) /
[bl616-toolchain.cmake](../cmake/bl616-toolchain.cmake) /
[bl616cl-toolchain.cmake](../cmake/bl616cl-toolchain.cmake)）驱动，完全不依赖
`platform/bouffalo_sdk/project.build`。bin 生成走 `bflb_add_executable()`
（[cmake/bflb_elf_to_bin.cmake](../cmake/bflb_elf_to_bin.cmake)）的 custom-command
sibling target —— BL602 链 bin 产物，BL616/BL616CL 链只到 ELF（flash 工具
pipeline 还没接）。

**Project CMakeLists 干净度：** [project/bouffalo/CMakeLists.txt](../project/bouffalo/CMakeLists.txt)
只有 `add_subdirectory(${AT_PROJECT_ROOT}/component …)` 一行拉 component tree（auto-loop
via [component/CMakeLists.txt](../component/CMakeLists.txt)），然后 `target_link_libraries(at PRIVATE bouffalo_sdk)`
一行 link。所有 cross-vendor lib + bridge + port adapter 都通过 bouffalo_sdk INTERFACE 一把
stitch 进来（见 [project/bouffalo/CMakeLists.txt](../project/bouffalo/CMakeLists.txt) 的 foreach 块）。

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

### BL616 / BL616CL 抽的还不完整
e2e build 通了，但走的是 **wl80211 路径**（不是 wifi6/fhost；用户明确：
"这两个平台的 wifi6 驱动都用 wl80211，不要再用 fhost 了"），只抽了一个 variant：

- **macsw 只有 default variant**：
  [component/bouffalo_sdk/macsw/lib/](../component/bouffalo_sdk/macsw/lib/) 只 copy 了
  `libmacsw_{bl616,bl616cl}_default.a` + `libmacsw_config_{bl616,bl616cl}_default.a`。
  upstream 还有 `coex` / `bridge` / `ack` 等其他 variant
  （[platform/bouffalo_sdk/components/wireless/macsw/lib/](../platform/bouffalo_sdk/components/wireless/macsw/lib/) 一共 16 个 .a）。要的话往
  lib/ 加 + 扩 [component/bouffalo_sdk/macsw/CMakeLists.txt](../component/bouffalo_sdk/macsw/CMakeLists.txt)
  里 FATAL_ERROR 的 available variants 列表即可。

- **wl80211 prebuilt 跳了 BL618DG**：只 copy 了 bl616 / bl616cl 的；BL618DG 整体不支持
  （多核 SoC，[component/bouffalo_sdk/bflb_ipc_rpmsg_ops/](../component/bouffalo_sdk/bflb_ipc_rpmsg_ops/) 的 OpenAMP
  dep 也没抽过来）。

- **wl80211 net80211 源码 port 跳了**：upstream
  [platform/bouffalo_sdk/components/wireless/wl80211/src/net80211/](../platform/bouffalo_sdk/components/wireless/wl80211/src/net80211/)
  是一份正在做的 FreeBSD net80211 port（per upstream `wl80211/CLAUDE.md`：所有文件都
  能编但功能没验证）。我们这边用 prebuilt `.a`，没抽 net80211 树。要切到 source build
  整树搬过来即可。

- **CONFIG_PM_MANAGER 默认 OFF**：[component/bouffalo_sdk/bflb_power_mgmt/CMakeLists.txt](../component/bouffalo_sdk/bflb_power_mgmt/CMakeLists.txt)
  里 `pm_manager.c` + `clock_manager.c` 都 gate 到 `CONFIG_PM_MANAGER`（默认 OFF）。
  pm_manager.c 之前因为找不到 `<wifi_mgmr_ext.h>` 卡了一阵子；现在 wl80211 抽过来了，
  技术上应该能 link 了 —— 但 audit 时这俩文件在我们 tree 里**零调用者**
  （唯一 consumer 是 tickless_accurate.c，它自己也 gate 着），所以 gate 关着。要做
  BT/Wi-Fi PS 联合的 tickless idle 才需要打开。

- **BL616/BL616CL flash pipeline 没接**：BL602 build 直接得
  `at_bl602_whole_flash_data.bin`（通过 `BLFlashCommand --build`），BL616/BL616CL
  只到 `.elf`。[cmake/bflb_elf_to_bin.cmake](../cmake/bflb_elf_to_bin.cmake) 里只有
  BL602 的 post-proc 分支；BL616/BL616CL 用 [tool/bflb_tools/](../tool/bflb_tools/) 里
  对应的工具就行，要扩 cmake 分支。

复现命令：
```sh
cd project/bouffalo
make CHIP=bl616   BOARD=bl616dk        # ELF
make CHIP=bl616cl BOARD=bl616cldk      # ELF
```

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
make CHIP=bl602   BOARD=bl602dk
make CHIP=bl616   BOARD=bl616dk
make CHIP=bl616cl BOARD=bl616cldk
```

三平台同时编（独立 build dir）：
```sh
(rm -rf build_bl602   && make CHIP=bl602   BOARD=bl602dk) &
(rm -rf build_bl616   && make CHIP=bl616   BOARD=bl616dk) &
(rm -rf build_bl616cl && make CHIP=bl616cl BOARD=bl616cldk) &
wait
```

预期 memory layout（`-O3` Release + ROMAPI on for BL602 + wl80211 path for BL616/CL）：

| 区域 | bl602/bl602dk | bl616/bl616dk | bl616cl/bl616cldk |
|---|---|---|---|
| fw_header | 176 B / 4 KB (4.30%) | 256 B / 4 KB (6.25%) | 256 B / 4 KB (6.25%) |
| rom_header | — | 4638 B / 6 KB (75.49%) | 4674 B / 6 KB (76.07%) |
| xip / rom | 588356 B / 1 MB (56.11%) | 589953 B / 4 MB (14.07%) | 531145 B / 4 MB (12.66%) |
| itcm_memory | 3900 B / 16 KB (23.80%) | — | — |
| dtcm_memory | 1028 B / 48 KB (2.09%) | — | — |
| ram | 53152 B / 176 KB (29.49%) | 92640 B / 319 KB (28.36%) | 68912 B / 223 KB (30.18%) |
| ram_wifi | 35824 B / 40 KB (87.46%) | 36128 B / 128 KB (27.56%) | 36128 B / 144 KB (24.50%) |
| hbn / ram_psram | — | 128 B / 4 KB + 0/4MB | 0 B (512 B 区) + 0/4MB |

参考 upstream `examples/wifi/sta/wifi_tcp` BL602 build（同样 wifi4 + lwip + freertos
工作量）跑出来 `itcm_memory: 3884 B / 16 KB`，跟我们 3900 B 同等量级 —— 不再有差距，
因为 ROMAPI 开了 + `-O3` 后基本对齐 upstream 配置。

## Session 2026-05-24 改动一览（按 commit 先到后）

`3c8c342 project/bouffalo: use component/CMakeLists.txt's auto-enumeration`
- 项目 CMakeLists 改成单行 `add_subdirectory(component)`，靠 [component/CMakeLists.txt](../component/CMakeLists.txt) 的 auto-loop 拉所有 component
- 补了缺失的 [cmake/bouffalo_sdk.cmake](../cmake/bouffalo_sdk.cmake) + [cmake/lwip_dhcpd.cmake](../cmake/lwip_dhcpd.cmake) glue
- 把 [cmake/lwip.cmake](../cmake/lwip.cmake) / [cmake/mbedtls.cmake](../cmake/mbedtls.cmake) / [cmake/lfs_easyflash.cmake](../cmake/lfs_easyflash.cmake) 里硬 FATAL 依赖检查换成 `include()` 自包含（include_guard 保证 idempotent），auto-loop alphabetical 顺序也能跑通

`ea7e068 project/bouffalo: collapse app link list, stitch project-level libs into bouffalo_sdk`
- App 那 14 个 explicit link 折成 `target_link_libraries(at PRIVATE bouffalo_sdk)`
- bflb_*_port + cross-vendor wrappers + bridges 用 foreach + `if(TARGET)` 一把 stitch 进 bouffalo_sdk INTERFACE

`6e426b9 bl616/bl616cl: extract wl80211 + macsw + bl_wpa_supplicant + rfparam, e2e build`
- 主要内容：从 `platform/bouffalo_sdk/components/wireless/` 抽 wl80211 + macsw + bl_wpa_supplicant 三个 component；从 `drivers/rfparam` 抽 rfparam
- adjacent fix：bflb_power_mgmt gate pm_manager 到 CONFIG_PM_MANAGER（+ empty.c stub 保 lib 非空）/ bflb_lwip_port PBUF_LINK_ENCAPSULATION_HLEN 48→388 / bl616_board_config + bl616cl_board_config 给 .ld.in 传 CONFIG_WIFI6 / 加 CONSOLE_UART_BAUDRATE 默认 / bflb_queue + bflb_tree 加 forwarder header
- `CMAKE_BUILD_TYPE=Release` 默认 → 修 BL616 rom_header overflow + 顺带 BL602 ITCM 7924→3900 / xip -24.7%
- proj.conf BL616/BL616CL 块 drop CONFIG_FHOST / CONFIG_MACSW_SELECT，改用 CONFIG_WL80211 + CONFIG_BL_WPA_SUPPLICANT + CONFIG_MACSW_SELECT=default

`5511970 bl602: enable CONFIG_ROMAPI to bring ITCM back to upstream 16K default`
- 真因 = ROMAPI 没开（BL602 BootROM 在 0x21010800 维护 jump table，SDK 通过 BFLB_USE_ROM_DRIVER 把 bl602_aon/common/ef_ctrl/sf_cfg_ext/sflash_ext/xip_sflash_ext 里 `ATTR_TCM_SECTION` 函数 ifdef 出去，留 10-byte thunk）
- 中间走过的弯路（rodata pointer table / LTO）也都在 doc/ITCM 那段记了

`3a7b3ec component/bouffalo_sdk: wire BL616 / BL616CL BT profiles + APP_VER macros`
- bflb_zypher_bluetooth/config 加 ble1m10s1bredr0 / m2s1 profile + BL616CL 的 EM_SIZE defsym
- bflb_lhal 从 PROJECT_SDK_VERSION 推 APP_VER_X/Y/Z

## 下次继续要看的（按重要性）

1. **BL616/BL616CL flash pipeline**：现在 build 只到 `.elf`，跑不上板。要给 [cmake/bflb_elf_to_bin.cmake](../cmake/bflb_elf_to_bin.cmake) 加 BL616 / BL616CL 分支（用 [tool/bflb_tools/](../tool/bflb_tools/) 里对应工具）。
2. **`src/main.cpp` BL616/BL616CL 路径**：现在能编只是因为它 include 的 `wifi_mgmr.h` / `bl_fw_api.h` / `btble_lib_api.h` 都 wire 上了，**runtime 没验过**。第一次上板要看哪些 init 顺序 / GPIO 引脚 / UART 配置要调。
3. **打开 `CONFIG_PM_MANAGER` 试一下**：BL616 上 wl80211 + lwip 都齐了，pm_manager.c 现在应该能 link，只是没人 call。如果要 BT/Wi-Fi PS 联合 tickless，需要打开 + 接 tickless_accurate。
4. **`CONFIG_SHELL=n`** 仍然关着；要打开得抽 `platform/bouffalo_sdk/components/shell/`（参考 bflb_mm/mm_debug.c 用 shell.h 那条）。
5. **CI 没接** BL616 / BL616CL；目前只有 BL602 在 CI 走 build。要加 matrix job + artifact upload。

## 重启 build 准备清单（新机器 / fresh clone）

```sh
git clone <repo>
cd at-project
git checkout experiment/driver-only
git submodule update --init --recursive    # 一定要 --recursive，submodule 里还有 nested
cd project/bouffalo
make CHIP=bl602 BOARD=bl602dk             # 验证：~30s 全量重建，产物 build_bl602/
make CHIP=bl616 BOARD=bl616dk             # 验证：~60s，build_bl616/at_bl616.elf
make CHIP=bl616cl BOARD=bl616cldk
```

Toolchain 自带（platform/toolchain_gcc_t-head_linux 是 submodule），不需要装系统 RISC-V gcc。
CMake 要 ≥ 3.16（实测 3.28.3 OK，CMP0156 那段 doc 提到 3.29+ 时序无关）。
