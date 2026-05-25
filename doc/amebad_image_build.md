# AmebaD 双核 boot+image build 流水线调查

目标：搞清楚 `platform/ameba-rtos-d` 是怎么把双核源码编出最终烧录用的几个 `.bin`，为后续自己写 CMake/CMake-ify 接管 build 系统铺路。重点放在 `km0_boot_all.bin` / `km4_boot_all.bin` 两个 bootloader 镜像。

参考兄弟文档：[ameba_image_download.md](ameba_image_download.md)（烧录侧）、[amebadplus_ota_image.md](amebadplus_ota_image.md)（AmebaDplus OTA 布局）、[bouffalo_elf_to_bin_pipeline.md](bouffalo_elf_to_bin_pipeline.md)（同位的 Bouffalo 流水线）。

## TL;DR

AmebaD 双核 build 完全镜像 — KM0 LP 和 KM4 HP 各有一个独立的 `project/` 目录、自己跑自己的 `make`。每个核都按相同 4 步走：

1. **link** — 各模块 `.o` + ROM lib + 一份 `.ld` → `target_loader.axf`（boot stage）或 `target_img2.axf`（application stage）
2. **objcopy 按段切** — `arm-none-eabi-objcopy` 按段名（`.ram_image1.*` / `.xip_image1.text` / `.ram_image2.*` / `.xip_image2.text` / `.ram_retention.*` / `.psram_image2.*`）拆成多个裸 `.bin`
3. **prepend 头部** — `gnu_utility/prepend_header.sh` 给每个裸 `.bin` 加 4 字节 LE 头（entry address 从 `.map` 里查 symbol）
4. **cat + 加密** — 把同 stage 的 prepend 文件按顺序 `cat` 成最终 `km{0,4}_*_all.bin`，再过 `image_tool/imagetool.sh` 决定要不要走 RSIP 加密 / SBOOT 签名 / RDP

每核做两次（bootloader + application），最终得到 4 个 `.bin`：
- `km0_boot_all.bin` — KM0 LP 1st-stage loader
- `km4_boot_all.bin` — KM4 HP 1st-stage loader
- `km0_image2_all.bin` — KM0 LP 应用 image
- `km4_image2_all.bin` — KM4 HP 应用 image（non-secure 侧；TrustZone 启用时还会有 `km4_image3_all.bin`）

烧录顺序：bootloader 段在 flash 低地址（KM0 @ 0x0800_0000 / KM4 @ 0x0800_4000），application 段紧随其后。

## 一、两核顶层 `make` 调用对比

`project/realtek_amebaD_va0_example/GCC-RELEASE/` 下两个独立目录：

| stage | KM0 LP (`project_lp/Makefile`) | KM4 HP (`project_hp/Makefile`) |
|---|---|---|
| 默认 `all` | `make -C asdk flashloader`<br>`make -C asdk bootloader`<br>`make -C asdk image2`（或 `ucps_image2`） | `make -C asdk image3_s`（TZ 启用时）<br>`make -C asdk bootloader`<br>`make -C asdk image2_ns` |
| 产物 | `km0_boot_all.bin` + `km0_image2_all.bin` | `km4_boot_all.bin` + `km4_image2_all.bin` (+ `km4_image3_all.bin`) |

`flashloader` 只是给 ROM-stage download 用的辅助镜像，不参与最终 flash 布局。`image3_s` 是 TrustZone secure side，AT 项目当前没启 TZ 所以可忽略。

## 二、`km{0,4}_boot_all.bin` 流水线全图

两核完全相同，下面用 KM0 作模板，KM4 把 `lp` 换成 `hp`、ld 文件换成 `rlx8721d_img1_s.ld` 即可：

```
        component/soc/realtek/amebad/bootloader/boot_ram_lp.c     ┐
        component/soc/realtek/amebad/bootloader/boot_flash_lp.c   │
        component/soc/realtek/amebad/usrcfg/rtl8721d_bootcfg.c    ├─→ build/ram/*.o
        component/soc/realtek/amebad/fwlib/ram_common/rtl8721d_wdg.c │   (make_subdirs_bootloader,
        component/soc/realtek/amebad/fwlib/ram_lp/rtl8721dlp_clk.c   ┘    只 5 个 .c，仅此而已)
                              │
                              │  copy_ld_img1: ld/rlx8721d_boot.ld → build/rlx8721d.ld
                              ▼
                arm-none-eabi-ld + ROM lib (rom.a) + ld script
                              │
                              ▼
                        target_loader.axf
                              │
       ┌──────────────────────┼──────────────────────┐
       │ strip + objcopy      │                      │
       ▼                      ▼                      ▼
   FROMELF                FROMELF                 nm/objdump
   -j .ram_image1.{entry, -j .xip_image1.text     → target_loader.map
       text,data}                                   (供 prepend 查 symbol 地址)
       → ram_1.bin            → xip_boot.bin
       │                       │
       │ prepend_header.sh     │ prepend_header.sh
       │  __ram_start_table_   │  __flash_boot_text_
       │   start__             │   start__
       ▼                       ▼
   ram_1_prepend.bin     xip_boot_prepend.bin
                              │
                              │  cat (xip 在前，ram 在后)
                              ▼
                        km0_boot_all.bin (raw)
                              │
                              │  imagetool.sh
                              │  - 如果 RSIP_ENABLE=1：EncTool rsip @ 0x08000000
                              │  - KM4 那侧还要：SBOOT_ENABLE=1 时 EncTool sboot
                              ▼
                        km0_boot_all.bin (最终)
```

规则定义位置：
- KM0：[platform/ameba-rtos-d/project/realtek_amebaD_va0_example/GCC-RELEASE/project_lp/asdk/Makefile:188-223](../platform/ameba-rtos-d/project/realtek_amebaD_va0_example/GCC-RELEASE/project_lp/asdk/Makefile#L188-L223)
- KM4：[platform/ameba-rtos-d/project/realtek_amebaD_va0_example/GCC-RELEASE/project_hp/asdk/Makefile:505-540](../platform/ameba-rtos-d/project/realtek_amebaD_va0_example/GCC-RELEASE/project_hp/asdk/Makefile#L505-L540)
- bootloader 源文件清单：`project_{lp,hp}/asdk/make/bootloader/Makefile`

KM4 bootloader 源文件比 KM0 多一个 `boot_trustzone_hp.c` + 一个 `rtl8721dhp_boot_trustzonecfg.c`，其他完全平行。

## 三、bootloader `.axf` 的实际组成

### 3.1 link 命令

两核完全一样（KM0 / KM4 各跑一遍）：

```
arm-none-eabi-gcc <LD_ARG>                                   # 仅 -L<toolchain>/lib
                  -T build/rlx8721d.ld                       # 由 copy_ld_img1 拷过来
                  -o target_loader.axf
                  ./build/ram/*.o                            # 5 个 bootloader .o
                  $(ROOTDIR)/lib/rom_acut/rom.a              # 默认置空，见下
```

注意：
- `LD = arm-none-eabi-gcc`（不是 `ld`），靠 gcc driver 隐式带 `libc` / `libgcc`
- **`lib/rom_acut/rom.a` 在仓库里不存在**。这是因为 `CONFIG_LINK_ROM_SYMB=y` 时 KM0 Makefile 把 `LINK_ROM_LIB` 置空，改用 `ld/rlx8721d_rom_symbol_acut.ld` 通过 `PROVIDE(symbol = 0xXXXXXXXX)` 直接把 ROM 函数地址灌进符号表 — ROM 已经固化在 SoC 内，不需要再 link 一份代码进 image
- `application/lib_*.a`（KM0 的 `lib_pmc_lp.a` / KM4 的 `lib_ac3.a` `lib_m4a_self.a` 等十几个音频/WiFi lib）**不在 bootloader 阶段链**，那些是 image2 stage 才用

### 3.2 编进 `.o` 的源码（每核 5 个 `.c`）

| 核 | 源文件 | 路径 | 行数 |
|---|---|---|---|
| KM0 | boot_ram_lp.c | [component/soc/realtek/amebad/bootloader/](../platform/ameba-rtos-d/component/soc/realtek/amebad/bootloader/boot_ram_lp.c) | 172 |
| KM0 | boot_flash_lp.c | 同上目录 | 508 |
| KM0 | rtl8721d_bootcfg.c | [component/soc/realtek/amebad/fwlib/usrcfg/](../platform/ameba-rtos-d/component/soc/realtek/amebad/fwlib/usrcfg/) | — |
| KM0 | rtl8721d_wdg.c | [component/soc/realtek/amebad/fwlib/ram_common/](../platform/ameba-rtos-d/component/soc/realtek/amebad/fwlib/ram_common/) | — |
| KM0 | rtl8721dlp_clk.c | [component/soc/realtek/amebad/fwlib/ram_lp/](../platform/ameba-rtos-d/component/soc/realtek/amebad/fwlib/ram_lp/) | — |
| KM4 | boot_ram_hp.c | [component/soc/realtek/amebad/bootloader/](../platform/ameba-rtos-d/component/soc/realtek/amebad/bootloader/boot_ram_hp.c) | 81 |
| KM4 | boot_flash_hp.c | 同上目录 | 701 |
| KM4 | boot_trustzone_hp.c | 同上目录 | 132 |
| KM4 | rtl8721dhp_boot_trustzonecfg.c | [component/soc/realtek/amebad/fwlib/usrcfg/](../platform/ameba-rtos-d/component/soc/realtek/amebad/fwlib/usrcfg/) | — |
| KM4 | rtl8721d_bootcfg.c | 同 KM0 | — |

→ KM0 bootloader 源码大致 700 行；KM4 大致 900 行。两核都极小。

### 3.3 隐式依赖（不进 `.o` 但编译/链接时必需）

bootloader 5 个 `.c` 第一行都是 `#include "ameba_soc.h"` — 这是 SDK 的 umbrella header，它的 #include 链最终拖入散落在整个 SDK 树里的 ~30 个头文件目录，远比"fwlib/include 自己"宽。

主要来源：

| 头文件源 | 路径 | 提供什么 |
|---|---|---|
| `ameba_soc.h` umbrella + chip register | [fwlib/include/](../platform/ameba-rtos-d/component/soc/realtek/amebad/fwlib/include/) (69 .h) | 入口、`rtl8721d_*.h` 寄存器宏、HAL inline、段属性宏（`BOOT_RAM_TEXT_SECTION` 等定义在 `section_config.h`） |
| Kconfig autoconf + 板级 opts | [project_lp/inc/inc_lp/](../platform/ameba-rtos-d/project/realtek_amebaD_va0_example/inc/inc_lp/) | `platform_autoconf.h`（rtl8721d.h 第一行 include）、`platform_opts.h`、`build_info.h`、`FreeRTOSConfig.h`、`main.h` |
| 散落 HAL / 应用 / RTOS 头 | `swlib/string` `swlib/include` `app/monitor/include` `app/xmodem` `cmsis` `misc` 等 | `memproc.h`、`diag.h`、`shell.h`、`rand.h`、`monitor_rom.h`、`xmodem_update_rom.h`、`core_armv8mbl.h`、`mpu_config.h` 等 |
| OS / 协议栈 / SSL | `os/freertos*` `common/{api,test,utilities,drivers/wlan,network/{lwip*,ssl/{polarssl,mbedtls-2.4.0,mbedtls_ram_map}*},mbed/...}` | 大部分 boot stage 不用，但 `rtl8721d_ota.h` 默认 define `HTTPS_OTA_UPDATE`，#include `mbedtls/version.h` 进了每个 TU |

→ 一句话：与其逐文件 grep 缺哪个 header，**直接 mirror SDK KM0 IFLAGS** 整段（[Makefile.include.gen:146-211](../platform/ameba-rtos-d/project/realtek_amebaD_va0_example/GCC-RELEASE/project_lp/asdk/Makefile.include.gen#L146-L211)，跳过 SDK 里不存在的 `cmsis/device` / `fwlib/rom_map_lp` / `wifi_fw/include` / `swlib/os_dep` / `swlib/std_lib` / `mbed/.../include` / `$(ROOTDIR)/../git_code/...` 等 7 个 MISS 路径）。`fwlib/include/` 已搬到项目内 [component/amebad/fwlib_headers/](../component/amebad/fwlib_headers/)（INTERFACE target `amebad_fwlib_headers`）；其余 SDK 路径仍 in-place reference，通过 INTERFACE target `amebad_sdk_includes`（[component/amebad/sdk_includes/CMakeLists.txt](../component/amebad/sdk_includes/CMakeLists.txt)）一次性供给所有 KM0 library / executable。

### 3.4 通过 ROM 提供的符号（不 link，靠 `.ld` PROVIDE）

bootloader 代码里调用的大多数实际函数（HAL 驱动 + libc-like + log + secure boot）**不在 5 个 `.o` 里**，而是通过两份 ROM symbol script 灌地址：

| script | 行数 | 用途 |
|---|---|---|
| [project_lp/asdk/ld/rlx8721d_rom_symbol_acut.ld](../platform/ameba-rtos-d/project/realtek_amebaD_va0_example/GCC-RELEASE/project_lp/asdk/ld/rlx8721d_rom_symbol_acut.ld) | 709 | KM0 ROM A-cut 暴露的符号表 |
| [project_hp/asdk/ld/rlx8721d_rom_symbol_acut_boot.ld](../platform/ameba-rtos-d/project/realtek_amebaD_va0_example/GCC-RELEASE/project_hp/asdk/ld/rlx8721d_rom_symbol_acut_boot.ld) | 320 | KM4 ROM A-cut bootloader 专用符号子集 |

ROM 里提供的（典型代码示例 `boot_ram_lp.c`/`boot_flash_lp.c` 调到的）：
- libc 包装：`_memset`, `_memcpy`, `_memcmp`, `_strcpy`, ...
- HAL 驱动：`RCC_PeriphClockCmd`, `Pinmux_Config`, `FLASH_RxCmd`, `FLASH_Read`, ...
- log：`DBG_8195A` 系列（实际走 LOGUART）
- secure boot：sboot 校验、RSIP 解密 hook
- power 序列：`CPU_PWR_SEQ` 表执行器

→ 这就是 bootloader `.o` 只有 700-900 行却能跑起来的原因 — 真正的 90% 代码量留在 SoC 内置 ROM 里。

### 3.5 三层依赖小结

| 层 | 形式 | 数量 | CMake-ify 时怎么处理 |
|---|---|---|---|
| 编进 `.o` | C 源码 | 5 个 .c / 核 | `add_executable(target_loader ...)` 列出 |
| Include only | C 头文件 | ~70 个 .h | `target_include_directories()` 指向 fwlib/include/ + bootloader/include/ + usrcfg/include/ |
| ROM PROVIDE | `.ld` 里的常量符号 | KM0: 709 行，KM4: 320 行 | 直接 `cat ld/rlx8721d_rom_symbol_acut.ld >> $(BUILD)/rlx8721d.ld`（保留原 SDK 文件即可） |

## 四、`km{0,4}_image2_all.bin`（application）流水线

跟 boot 是同一套机制，只是换段名 + 换 ld：

| 维度 | bootloader stage | image2 stage |
|---|---|---|
| ld 模板 (KM0) | `ld/rlx8721d_boot.ld` | `ld/rlx8721d_img2.ld` |
| ld 模板 (KM4) | `ld/rlx8721d_img1_s.ld` | `ld/rlx8721d_img2_is.ld`<br>（TZ 时 `rlx8721d_img2_tz.ld`） |
| axf 名 | `target_loader.axf` | `target_img2.axf` |
| 抽段（FROMELF） | `.ram_image1.{entry,text,data}` → `ram_1.bin`<br>`.xip_image1.text` → `xip_boot.bin` | `.ram_image2.{entry,text,data}` → `ram_2.bin`<br>`.xip_image2.text` → `xip_image2.bin`<br>**KM0 多**：`.ram_retention.{text,entry}` → `ram_retention.bin`<br>**KM4 多**：`.psram_image2.*` → `psram_2.bin` |
| prepend symbol | `__ram_start_table_start__`<br>`__flash_boot_text_start__` | `__ram_image2_text_start__`<br>`__flash_text_start__`<br>`__retention_entry_func__`（仅 KM0）<br>`__psram_image2_text_start__`（仅 KM4） |
| cat 顺序 → 最终 bin | xip_boot + ram_1 → `km{0,4}_boot_all.bin` | xip_image2 + ram_2 [+ ram_retention 或 + psram_2] → `km{0,4}_image2_all.bin` |
| 编入的 module 范围 | 仅 `bootloader` 一个模块（5 个 .c） | `make_subdirs` — 全部 33（KM4） / 13（KM0）个 module |
| imagetool.sh 加密 | RSIP @ 0x0800_0000 (KM0) / 0x0800_4000 (KM4)<br>KM4 还可 SBOOT 签名 | RSIP @ 0x0c00_0000 (KM0) / 0x0e00_0000 (KM4) — XIP 区域 |

`km0_image2_all.bin` build 完之后还会被 `OTAPREPENDTOOL`（`gnu_utility/prepend_ota_header.sh`）合并进 KM4 一侧的 `km0_km4_image2.bin`，再由 KM4 的 OTA 流程封成 `OTA_All.bin`。

## 五、工具清单

| 工具 | 路径 | 类型 | 干啥 |
|---|---|---|---|
| `LD` / `objcopy`(`FROMELF`) / `NM` / `OBJDUMP` / `STRIP` | toolchain（`asdk/`） | binutils | 标准链接 + 段抽取 |
| `prepend_header.sh` | `gnu_utility/prepend_header.sh` | bash | 从 `.map` 查 symbol 地址，加 4-byte LE 头 |
| `prepend_ota_header.sh` | `gnu_utility/prepend_ota_header.sh` | bash | 给 image2 加 OTA header |
| `imgpadl` (Linux) / `imgpad.exe` (Windows) | `gnu_utility/imgpad*` | C 编译的可执行 | 对齐 bin 到指定 byte 边界（gen_sim_txt 用到 4/8/32） |
| `EncTool` (Linux) / `EncTool.exe` | `gnu_utility/image_tool/EncTool*` | 闭源二进制 | RSIP 加密 / SBOOT 签名 / RDP — Realtek 自家加密工具 |
| `imagetool.sh` | `gnu_utility/image_tool/imagetool.sh` | bash | 按 IMAGE_FILENAME 决定调哪种 EncTool 操作 |
| `security_config.sh` | `project_{lp,hp}/GCC-RELEASE/security_config.sh` | bash 配置 | 开关 `RSIP_ENABLE` / `SBOOT_ENABLE` / `RDP_ENABLE` + 提供 key |
| `code_analyze.py` | `asdk/code_analyze.py` | python | 仅 KM0 bootloader 阶段调用，做 size 统计（不影响产物） |

## 六、image 目录产物清单（一次成功 build 之后）

`project_lp/asdk/image/`：

```
km0_boot_all.bin         ← 1st-stage loader（用户问的）
km0_image2_all.bin       ← KM0 应用 image
km0_km4_image2.bin       ← OTA-prepend 后等待 KM4 拼接
OTA_All.bin              ← 跟 KM4 那侧应该完全相同（make 完会同步）
ram_1.bin / ram_1_prepend.bin                 ← bootloader stage 中间产物
xip_boot.bin / xip_boot_prepend.bin           ← bootloader stage 中间产物
ram_2.bin / ram_2_prepend.bin                 ← image2 stage 中间产物
xip_image2.bin / xip_image2_prepend.bin       ← image2 stage 中间产物
ram_retention.bin / ram_retention_prepend.bin ← KM0 独有
target_loader.axf / target_img2.axf           ← 两个 stage 的 ELF
target_pure_*.axf                             ← strip 之后的副本
target_*.map / target_*.asm                   ← nm + objdump 输出
code_size_*.map / ram_size.txt                ← size 统计
obj_list.txt                                  ← 实际 link 的 .o 列表
```

`project_hp/asdk/image/`：跟 KM0 几乎对称，差异：
- 不产 `ram_retention.bin`
- 多产 `psram_2.bin` / `psram_2_prepend.bin`（KM4 用 PSRAM）
- 不产 `km0_image2_all.bin`，但产 `km0_km4_image2.bin`（KM4 那边把 KM0 image2 收过来拼）
- TZ 启用时多产 `km4_image3_all.bin`（secure 侧）

## 七、烧录布局（4 MB flash, partition 约定）

详细 partition 表见 [reference_amebad_flash_layout 记忆](../../../.claude/projects/-home-zxf-at-project/memory/reference_amebad_flash_layout.md)。简化记法：

```
0x0800_0000  ─┬─ km0_boot_all.bin
              │
0x0800_4000  ─┼─ km4_boot_all.bin
              │
0x0800_C000  ─┼─ km0_km4_image2.bin (= km0_image2 + km4_image2 拼合)
              │
              ⋮  OTA region / FTL / LFS / NV
              │
0x083F_FFFF  ─┘  (4 MB 边界)
```

RSIP 加密地址跟 partition 起始一一对应（imagetool.sh 里硬编码：boot 用 0x0800_0000/0x0800_4000，image2 用 0x0c00_0000/0x0e00_0000 — 是 XIP cache 映射地址而非 flash 物理地址）。

## 八、自己 CMake-ify 时的取舍

如果将来要把这套流水线接管，最低风险的切割点是：

- **保留 `gnu_utility/` 整目录**（prepend、imgpad、imagetool、EncTool）— 全是 shell + 平台二进制，跟 build system 解耦，CMake 里直接 `add_custom_command` 调即可
- **保留 `ld/` 整目录** — linker script 跟 chip layout 强绑定，照搬
- **保留 `usrcfg/rtl8721d_bootcfg.c`** — 板级 boot 配置，板厂改了就失效
- **可重写**：33+13 个 `make/<module>/Makefile` 是真正的"屎山" — 改成 CMake 的 `add_library(<module> OBJECT ...)` + glob 源文件
- **可保留 SDK 自带 KM0 镜像不重编**：KM0 业务代码 = 0（13 模块全是 boot/cmsis/os/pmc/monitor/xmodem），AT 项目用不到，直接拿 SDK build 出来的 `km0_boot_all.bin` + `km0_image2_all.bin` 当 binary blob 即可，只 CMake-ify KM4 一侧

→ 真正"双核移植"的工作量集中在 KM4，KM0 可以当 black box。这跟 Realtek 自家 Arduino package 把 KM0 冻成 `lib_pmc_lp.a` + bin 是同一个 tradeoff。
