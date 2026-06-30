# Bouffalo SDK 实际依赖清单

本项目（at-project）在 `project/bouffalo/` 下针对 **BL602 / BL616 / BL616CL** 三颗芯片各产出一份固件。本文档列出**真正被链入 `.elf` 的 `.o` 文件**（区分"编译了但被 linker GC 丢弃"和"实际进了 binary"）。

数据采集自 `platform/bouffalo_sdk @ at-v2.3.26`（3124b3755），所有 6 个平台编译通过：

| 平台 | 输出 | 大小 |
|---|---|---|
| BL602 | `project/bouffalo/build_bl602/build_out/at_bl602.elf` | 6,830,772 B |
| BL616 | `project/bouffalo/build_bl616/build_out/at_bl616.elf` | 8,061,184 B |
| BL616CL | `project/bouffalo/build_bl616cl/build_out/at_bl616cl.elf` | 7,828,320 B |
| AmebaD | `project/amebad/out/Image_All.bin` | ✅ |
| AmebaDplus | `project/ameba/build_RTL8711Dx/Image_All.bin` | ✅ |
| AmebaGreen2 | `project/ameba/build_RTL8711F/Image_All.bin` | ✅ |

本文档只覆盖 Bouffalo 三颗芯片；Realtek 阵营见 [realtek_network_stack_eval.md](realtek_network_stack_eval.md)。

## 一、链接模型说明

SDK 的最终链接行（来自 [cmake/extension.cmake:329](../platform/bouffalo_sdk/cmake/extension.cmake)）：

```
target_link_libraries(${proj_name}.elf
    -Wl,--whole-archive ${SORTED_SDK_LIBS} app -Wl,--no-whole-archive
    zlibstatic)
```

意味着：
- 所有 SDK_LIBS + libapp + vendor prebuilt `.a` 走 **`--whole-archive`** —— `.a` 内每个 `.o` 都被强制 include 到 link 输入。
- `libzlibstatic` 走普通链接（按需）。
- 编译用 `-ffunction-sections -fdata-sections`，链接用 `-Wl,--gc-sections` —— 没被引用的 fine-grained `.text.func` / `.rodata.var` section 被丢弃。

所以"编译了但实际没链接的"分两层：
1. **`.o` 粒度**：whole-archive 强制 include，但若 `.o` 内**所有 section 都被 GC**，则该 `.o` 不出现在 `.elf` 的 LOAD 列表。下面"未链入 .o"清单就是这类。
2. **section 粒度**：`.o` 内具体 function/data section 被 GC（最常见）。在 `at_<chip>.map` 的 `Discarded input sections` 段可查（11 MB map 文件，BL616 上有 ~40000 行 discarded sections）。本文档不展开 section 级。

LTO（`-flto`，由 `CONFIG_GCC_COMPILE_LTO=y` 启用）让 SDK 自己 build 的 `.a` 内 `.o` 在 link 时被合并/重写到 `/tmp/at_<chip>.elf.*.ltrans*.o`，map 文件里仍能看到原 `.a(file.o)` 引用（因为 LTO 之前的初始 link 记录保留）。Vendor 预编译 `.a`（如 `libmacsw_*.a`、`libbl*_phyrf.a`、`libpka_*.a`）不参与 LTO。

## 二、每芯片：实际链入 `.elf` 的 `.a` 清单

每个 chip 的 `.elf` link.txt 完整 archive 列表。**所有这些 `.a` 都被 `--whole-archive` 强制链入**（zlibstatic 除外）。

### BL602（28 个 archive）

| Lib | 来源 | 编译 .o | 链入 .o | 未用 |
|---|---|---:|---:|---:|
| libsysinit | `components/sysinit` | 1 | 1 | 0 |
| libmm | `components/mm` | 4 | 4 | 0 |
| liblibc | `components/libc` | 56 | 56 | 0 |
| libutils | `components/utils` | 19 | 19 | 0 |
| libshell | `components/shell` | 2 | 2 | 0 |
| libfreertos | `components/os/freertos` | 11 | 11 | 0 |
| liblhal | `drivers/lhal` | 34 | 34 | 0 |
| libsys | `drivers/sys` | 1 | 1 | 0 |
| librfparam | `drivers/rfparam` | 2 | 2 | 0 |
| libstd | `drivers/soc/bl602/std` | 22 | 21 | **1** (bl602_clock.c) |
| liblwip | `components/net/lwip/lwip` | 51 | 51 | 0 |
| libping | `components/net/lwip/lwip_apps/ping` | 1 | 1 | 0 |
| libmbedtls | `components/crypto/mbedtls` | 117 | 117 | 0 |
| liblittlefs | `components/fs/littlefs` | 4 | 4 | 0 |
| libcoex | `components/wireless/coex` | 3 | 3 | 0 |
| libblestack | `components/wireless/bluetooth/blestack` | 43 | 42 | **1** (utils.c) |
| libmanager | `components/wireless/wifi4/manager` | 21 | 21 | 0 |
| libblcrypto_suite | `components/wireless/wifi4/blcrypto_suite` | 11 | 11 | 0 |
| libbflb_adapter | `components/wireless/wifi4/wifi4_adapter/bflb_adapter` | 7 | 7 | 0 |
| libwifi_hosal | `components/wireless/wifi4/wifi4_adapter/wifi_hosal` | 3 | 3 | 0 |
| libwpa_supplicant | `components/wireless/wifi4/wpa_supplicant` | 45 | 45 | 0 |
| libapp | (内部，源自 `bsp/`) | 4 | 4 | 0 |
| libzlibstatic | `component/zlib` | 15 | 10 | **5** |
| libblecontroller_bl602_m1s1 | `components/wireless/bluetooth/blecontroller/lib/` (prebuilt) | 19 | 19 | 0 |
| libfirmware | `components/wireless/wifi4/firmware/lib/` (prebuilt) | 1 | 1 | 0 |
| libpka_bl602 | `drivers/lhal/src/pka/` (prebuilt) | 18 | 4 | **14** |
| libbl602_phyrf | `drivers/soc/bl602/phyrf/lib-gcc-10.2.0-.../libbl602_phyrf.a` (prebuilt) | 16 | 9 | **7** |

**BL602 共链入 600 个 SDK-side `.o`**（不含 toolchain libc/libgcc/libm/libstdc++）。

### BL616（30 个 archive）

| Lib | 来源 | 编译 .o | 链入 .o | 未用 |
|---|---|---:|---:|---:|
| libsysinit | `components/sysinit` | 1 | 1 | 0 |
| libmm | `components/mm` | 4 | 4 | 0 |
| liblibc | `components/libc` | 56 | 56 | 0 |
| libutils | `components/utils` | 19 | 19 | 0 |
| libshell | `components/shell` | 2 | 2 | 0 |
| libfreertos | `components/os/freertos` | 11 | 11 | 0 |
| liblhal | `drivers/lhal` | 47 | 47 | 0 |
| libsys | `drivers/sys` | 12 | 12 | 0 |
| librfparam | `drivers/rfparam` | 2 | 2 | 0 |
| libstd | `drivers/soc/bl616/std` | 23 | 22 | **1** (bl616_clock.c) |
| liblwip | `components/net/lwip/lwip` | 51 | 51 | 0 |
| libping | `components/net/lwip/lwip_apps/ping` | 1 | 1 | 0 |
| libmbedtls | `components/crypto/mbedtls` | 117 | 117 | 0 |
| liblittlefs | `components/fs/littlefs` | 4 | 4 | 0 |
| libcoex | `components/wireless/coex` | 3 | 3 | 0 |
| libblestack | `components/wireless/bluetooth/blestack` | 41 | 40 | **1** (utils.c) |
| libfhost_config | `components/wireless/wifi6/fhost_config` | 1 | 1 | 0 |
| libmacsw_plat | `components/wireless` | 1 | 1 | 0 |
| libmacsw_os_adapter | `components/wireless/wifi6/macsw_os_adapter` | 2 | 2 | 0 |
| libwifi6_lwip_adapter | `components/wireless/wifi6/wifi6_lwip_adapter` | 6 | 6 | 0 |
| libwpa_supplicant | `components/wireless/wifi6/wpa_supplicant` | 106 | 106 | 0 |
| libapp | (内部，源自 `bsp/`) | 8 | 8 | 0 |
| libzlibstatic | `component/zlib` | 15 | 9 | **6** |
| libbtblecontroller_bl616_ble1m10s1bredr0 | `components/wireless/bluetooth/btblecontroller/lib/` (prebuilt) | 111 | 111 | 0 |
| libmacsw_bl616_default | `components/wireless/macsw/lib/` (prebuilt) | 105 | 105 | 0 |
| libmacsw_config_bl616_default | `components/wireless/macsw/lib/` (prebuilt) | 1 | 1 | 0 |
| libfhost_bl616_default | `components/wireless/wifi6/fhost/lib/` (prebuilt) | 13 | 13 | 0 |
| libcsi_xt900p32f_dsp | `drivers/lhal/include/arch/risc-v/t-head/csi_dsp/lib/` (prebuilt) | 319 | 319 | 0 |
| libpka_bl616 | `drivers/lhal/src/pka/` (prebuilt) | 20 | 4 | **16** |
| libbl616_phyrf | `drivers/soc/bl616/phyrf/lib-gcc_10.2.0-.../libbl616_phyrf.a` (prebuilt) | 24 | 16 | **8** |

**BL616 共链入 775 个 SDK-side `.o`**。

### BL616CL（30 个 archive）

| Lib | 来源 | 编译 .o | 链入 .o | 未用 |
|---|---|---:|---:|---:|
| libsysinit | `components/sysinit` | 1 | 1 | 0 |
| libmm | `components/mm` | 4 | 4 | 0 |
| liblibc | `components/libc` | 56 | 56 | 0 |
| libutils | `components/utils` | 19 | 19 | 0 |
| libshell | `components/shell` | 2 | 2 | 0 |
| libfreertos | `components/os/freertos` | 11 | 11 | 0 |
| liblhal | `drivers/lhal` | 47 | 47 | 0 |
| libsys | `drivers/sys` | 14 | 14 | 0 |
| librfparam | `drivers/rfparam` | 2 | 2 | 0 |
| libstd | `drivers/soc/bl616cl/std` | 17 | 17 | 0 |
| liblwip | `components/net/lwip/lwip` | 51 | 51 | 0 |
| libping | `components/net/lwip/lwip_apps/ping` | 1 | 1 | 0 |
| libmbedtls | `components/crypto/mbedtls` | 117 | 117 | 0 |
| liblittlefs | `components/fs/littlefs` | 4 | 4 | 0 |
| libcoex | `components/wireless/coex` | 3 | 3 | 0 |
| libblestack | `components/wireless/bluetooth/blestack` | 41 | 40 | **1** (utils.c) |
| libfhost_config | `components/wireless/wifi6/fhost_config` | 1 | 1 | 0 |
| libmacsw_plat | `components/wireless` | 1 | 1 | 0 |
| libmacsw_os_adapter | `components/wireless/wifi6/macsw_os_adapter` | 2 | 2 | 0 |
| libwifi6_lwip_adapter | `components/wireless/wifi6/wifi6_lwip_adapter` | 6 | 6 | 0 |
| libwpa_supplicant | `components/wireless/wifi6/wpa_supplicant` | 106 | 106 | 0 |
| libapp | (内部，源自 `bsp/`) | 8 | 8 | 0 |
| libzlibstatic | `component/zlib` | 15 | 9 | **6** |
| libbtblecontroller_bl616cl_m2s1 | `components/wireless/bluetooth/btblecontroller/lib/` (prebuilt) | ~110 | ~110 | 0 |
| libmacsw_bl616cl_default | `components/wireless/macsw/lib/` (prebuilt) | ~100 | ~100 | 0 |
| libmacsw_config_bl616cl_default | `components/wireless/macsw/lib/` (prebuilt) | 1 | 1 | 0 |
| libfhost_bl616cl_default | `components/wireless/wifi6/fhost/lib/` (prebuilt) | ~13 | ~13 | 0 |
| libmultimedia_bl616cl | `drivers/lhal/src/multimedia/lib/` (prebuilt) | ✅ | ✅ | — |
| libpec_v2_bl616cl | `drivers/lhal/src/pec_v2/lib/` (prebuilt) | ✅ | ✅ | — |
| libpka_bl616cl | `drivers/lhal/src/pka/` (prebuilt) | ~20 | 4 | **~16** |
| libbl616cl_phyrf | `drivers/soc/bl616cl/phyrf/lib-gcc_10.2.0-.../libbl616cl_phyrf.a` (prebuilt) | ~24 | ~16 | **~8** |

**BL616CL 共链入 784 个 SDK-side `.o`**。

> 注：BL616CL **没有** `libcsi_xt900p32f_dsp`（其 SoC 不带 CSI DSP）。
> BL616CL **独有** `libmultimedia_bl616cl` + `libpec_v2_bl616cl` (BL616 无)。

## 三、`.o` 粒度的未链入清单

下面这些 `.o` 在 `.a` 内被编译但在 `.elf` 内**完全不存在**（所有 section 都被 GC 丢弃）：

### 跨 chip 共有

| Lib | 未链入 .o | 原因推测 |
|---|---|---|
| **libzlibstatic** | `crc32.c.obj`、`gzclose.c.obj`、`gzlib.c.obj`、`gzread.c.obj`、`gzwrite.c.obj`、`infback.c.obj` (BL616/BL616CL) | at-project 仅用 zlib inflate/deflate 路径，不用 `gz*` 文件 IO 接口和 backward inflate；BL602 多 1 个（CRC8/CRC32 实现来源不同） |
| **libblestack** | `utils.c.obj` | BLE 工具函数没被引用 |
| **libstd** | `bl<CHIP>_clock.c.obj` (BL602/BL616；BL616CL 不缺) | clock driver 一部分被 `port/` 版本替代 |
| **libpka_${CHIP}** (prebuilt) | ~14-16 个 PKA 算法 `.o`（ed25519、x25519、curve448 等大整数算法） | mbedTLS 只用了 ECP_DP_SECP256R1，其他曲线 PKA 实现没被引 |
| **libbl*_phyrf** (prebuilt) | 7-8 个 PHY 子文件 | 未启用 BLE-only 配置 / 部分 cal 路径 |

完整 `.o` 级清单：

- BL602: [doc/bouffalo_deps/bl602.txt](bouffalo_deps/bl602.txt) — 600 个 SDK-side `.o`
- BL616: [doc/bouffalo_deps/bl616.txt](bouffalo_deps/bl616.txt) — 775 个 SDK-side `.o`
- BL616CL: [doc/bouffalo_deps/bl616cl.txt](bouffalo_deps/bl616cl.txt) — 784 个 SDK-side `.o`

每行格式：`<lib.a>  <file.c.obj>`。这就是 GC 之后真实出现在 `.elf` 里的 `.o`。

## 四、完全未启用的 SDK 子目录（`components/`、`drivers/` 内）

下列 SDK 路径在 BL602 / BL616 / BL616CL 三个 build 里**都不出现**——没生成 `.a`、没出现在 link.txt：

```
components/ai          components/multimedia   components/ipc
components/graphics    components/usb          components/fota
components/iperf       components/lzma         components/debug   (CONFIG_BACKTRACE=n)

components/net/lwip/lwip_apps/{mdns,httpd,dhcpd,sntp,iperf,tftp,...}
components/net/netbus  components/net/nethub   components/net/lib

# Wireless 按 chip 互斥的:
BL602    不用 components/wireless/wifi6/* + components/wireless/macsw/* + .../fhost/*
BL616+   不用 components/wireless/wifi4/* + components/wireless/bluetooth/blecontroller/*
```

这些是 [shared_components_eval.md](shared_components_eval.md) 第七节「短期可剪裁」清单的实证依据。

## 五、跨芯片依赖重叠度

| 库 | BL602 | BL616 | BL616CL | 备注 |
|---|:---:|:---:|:---:|---|
| libsysinit / libmm / liblibc / libutils / libshell | ✅ | ✅ | ✅ | 14 个共用组件 |
| libfreertos / liblhal / libsys / librfparam | ✅ | ✅ | ✅ | |
| liblwip / libping / libmbedtls / liblittlefs | ✅ | ✅ | ✅ | |
| libcoex / libblestack | ✅ | ✅ | ✅ | |
| libstd | bl602 | bl616 | bl616cl | chip-specific |
| libapp (bsp glue) | board-specific | board-specific | board-specific | |
| libzlibstatic | ✅ | ✅ | ✅ | at-project component |
| **WiFi stack** | wifi4 (manager + blcrypto_suite + bflb_adapter + wifi_hosal + wpa_supplicant) | wifi6 (fhost_config + macsw_plat + macsw_os_adapter + wifi6_lwip_adapter + wpa_supplicant) | wifi6 (同 BL616) | wifi4 vs wifi6 二分 |

总结：三颗 chip 共用 **14 个源码组件 + libapp + libzlibstatic**；WiFi 路径完全二分；其余按 chip 选 binary。

## 六、含义

1. **共用 14 个 SDK 组件**（chips 通用，配置无差异）：这是 `at-project/component/` 抽出的最佳候选——三颗 chip 100% 复用，未来还可与 Realtek 阵营的同类组件合并。
2. **未启用的 SDK 子目录**（约 12 个 `components/*` + 部分 `wireless/*` chip-mutually-exclusive）：这些可以从 SDK fork 中剪裁，对本项目无影响。
3. **`.o` 粒度未链入**主要集中在 `libpka_*` 和 `libbl*_phyrf`：vendor binary 已经把所有 PHY 子模块 + PKA 算法编进同一个 `.a`，但 mbedTLS / WiFi PS 只用一部分。无法 source-level 优化（vendor 不发源码），但 linker GC 已经丢掉了不用的 section，所以 ELF size 没浪费。
4. **section-level dead code** 才是 GC 真正在工作的地方（map 文件 Discarded sections 段有 ~10000+ 行/chip）。这是 SDK 整库编译 + 全 whole-archive 策略下的常态——开发期对 build size 影响不大（.a 体积大），但 ELF size 最终是按真实引用图算的。

## 附：数据采集命令

```bash
# 1. 切回 SDK 基线分支
cd platform/bouffalo_sdk && git checkout at-v2.3.26 && cd -

# 2. 编 6 个平台
make -C project all

# 3. 列每个 chip 的链入 .a (top-level)
for chip in bl602 bl616 bl616cl; do
    echo "=== $chip ==="
    cat project/bouffalo/build_$chip/CMakeFiles/at_$chip.elf.dir/link.txt \
        | tr ' ' '\n' | grep '\.a$' | sort -u
done

# 4. 提取每个 chip 实际链入的 .o (区分编译 vs GC 之后)
for chip in bl602 bl616 bl616cl; do
    grep -oE '[^[:space:]]+\.a\([^)]+\)' project/bouffalo/build_$chip/build_out/at_$chip.map \
        | sort -u \
        | sed 's|.*/\([^/]*\)\.a(\([^)]*\))|\1.a  \2|' \
        > doc/bouffalo_deps/$chip.txt
done

# 5. 对比编译 vs 链入
AR=platform/toolchain_gcc_t-head_linux/bin/riscv64-unknown-elf-ar
for chip in bl602 bl616 bl616cl; do
    echo "=== $chip ==="
    find project/bouffalo/build_$chip/build_out/lib -name "*.a" | while read alib; do
        libname=$(basename $alib)
        compiled=$($AR t $alib 2>/dev/null | wc -l)
        linked=$(grep "^${libname}  " doc/bouffalo_deps/$chip.txt | wc -l)
        printf "%-30s compiled=%4d  linked=%4d  unused=%4d\n" \
            "$libname" "$compiled" "$linked" "$((compiled - linked))"
    done | sort -k3 -rn
done
```
