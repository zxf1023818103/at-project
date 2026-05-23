# Bouffalo ELF → BIN 流水线调查

目标：把 `bouffalo_sdk` 里 ELF→flashable BIN 的工具链单独抽到 `project/bouffalo/tools/`，让 BIN 生成**不再依赖 `platform/bouffalo_sdk/`**。

本文档先把现状拆清楚，识别工具/配置/CMake glue 三层依赖，给出抽出方案与风险点。

## TL;DR

`project/bouffalo/build_<chip>/build_out/whole_flash_data.bin` 是终态产物。SDK 现在 4 步生成它：

1. **CMake `${proj_name}.elf` POST_BUILD**（`extension.cmake:333`）— `riscv64-unknown-elf-objcopy -Obinary` 把 ELF 转 raw `.bin`。**只用 toolchain，不依赖 SDK 本体**。
2. **CMake `combine` target**（`bflb_flash.cmake:111`）— 调 `bflb_fw_post_proc-ubuntu`，传入 chip 名 + 板配置目录，把刚才的 raw `.bin` 加 bootheader、生成 `at_<chip>.bin{,.hash,.ota,.xz}`，并把 `boot2_*.bin` / `mfg_*.bin` / `partition.bin` / `efusedata*.bin` 拷到 `build_out/`。
3. **CMake `post_build` target**（`bflb_flash.cmake:108-200`）— 一系列条件性步骤（DWARF CFI 嵌入、certificate bundle、LFS 生成、LP_FW patch、dualcore multi-bin）。**at-project 当前 proj.conf 下只触发 `CONFIG_BACKTRACE` 这一条**，会调 `unwind_6byte_table.py` + `multi_bins.py`。
4. **`project/bouffalo/tools/build_whole.py`**（我们自己写的）— 把 step 2/3 产物拼成 4 MB `whole_flash_data.bin`。它**已经独立于 SDK CMake 体系**，但仍调用 SDK 的 `BLFlashCommand-ubuntu` 作底层。

→ 真正 SDK-bound 的是 step 2/3/4 里的**工具二进制 + chip 配置数据**。step 1 用 toolchain 命令就行，跟 SDK 无关。

## 一、流水线全景（按 build target 顺序）

```
                  src/*.cpp + bsp/ + SDK libs
                              │
                              ▼
            [CMake link] riscv64-unknown-elf-gcc -T <linker.ld>
                              │
                              ▼
                    build_out/at_<chip>.elf
                              │
       ┌──────────────────────┼──────────────────────┐
       │                      │                      │
       │  POST_BUILD on       │  combine target      │  post_build target
       │  ${proj}.elf         │  (bflb_flash.cmake)  │  (bflb_flash.cmake)
       │  (extension.cmake)   │                      │
       │  toolchain only      │  bflb_fw_post_proc   │  unwind_6byte_table.py
       │                      │                      │  multi_bins.py
       ▼                      ▼                      ▼
  at_<chip>.bin          at_<chip>.bin               at_<chip>.bin
  (raw flash image)      (with bootheader,           (with embedded DWARF CFI
                          + .hash/.ota/.xz)          appended for backtrace)
                         + boot2_*.bin
                         + mfg_*.bin
                         + partition.bin
                         + efusedata*.bin
                              │
                              ▼
               project/bouffalo/tools/build_whole.py
                  (compose flash_prog_cfg.ini, run BLFlashCommand --build,
                   pad to 4 MB, embed lfs.bin into PSM partition)
                              │
                              ▼
              build_out/whole_flash_data.bin   ← 最终产物
```

注：`combine` 跟 `post_build` 实际执行顺序是 `post_build → combine`（看 `project.build:147-150`：先 `pre_build`、再 main build、再 `post_build`、最后 `combine`）。

## 二、SDK 实际被依赖的资源（按类型分类）

### A. 平台特定的工具二进制（PyInstaller 打包，dynamic ELF / Mach-O / PE）

| 工具 | 路径 | 用途 | Linux x86_64 大小 |
|---|---|---|---:|
| **bflb_fw_post_proc** | `tools/bflb_tools/bflb_fw_post_proc/bflb_fw_post_proc-{ubuntu,macos,exe}` | 给 raw `.bin` 加 bootheader、生成 .hash/.ota/.xz、附带把 boot2/partition/mfg/efusedata 写出来 | 9.4 MB |
| **BLFlashCommand** | `tools/bflb_tools/bouffalo_flash_cube/BLFlashCommand-{ubuntu,macos,exe,arm,freebsd}` | 按 `flash_prog_cfg.ini` 拼合多个 .bin 成 `whole_flash_data.bin` | 9.5 MB |
| **patch_lpfw** | `tools/lpfw/patch_lpfw-{ubuntu,macos,exe}` | 把 LP_FW 二进制 patch 进主 firmware（我们 **不用**） | 2.9 MB |
| **mklfs** | `tools/genlfs/mklfs-{ubuntu,macos,exe}` | 生成 littlefs 镜像（我们已经直接在 `project/Makefile` 调） | 80 KB |
| **genromfs** | `tools/genromfs/genromfs-{ubuntu,macos,exe}` | ROMFS 镜像（我们 **不用**） | 18 KB |

每个工具都需要 **3 个平台变体**（Linux / macOS / Windows）齐备才能跨平台开发。`BLFlashCommand` 还多两个（aarch64, FreeBSD）。

**总体积**：
- `tools/bflb_tools/bflb_fw_post_proc/` = **29 MB**
- `tools/bflb_tools/bouffalo_flash_cube/` = **295 MB**（含 `chips/<chip>/` 数据库）
- `tools/lpfw/` = 8.9 MB
- `tools/genlfs/` = 256 KB
- `tools/genromfs/` = ~60 KB

### B. BLFlashCommand 的 chip 数据库（`chips/<chip>/`）

每颗芯片一个独立子目录，**BLFlashCommand 必须能找到它**。每个含：

| 子目录 | 内容 | 是否必需（生成 whole_flash_data.bin） |
|---|---|---|
| `efuse_bootheader/` | `efuse_bootheader_cfg.conf`、`bootheader_group0.bin`、`clock_para.bin` | ✅ bflb_fw_post_proc 用这个生成 bootheader |
| `eflash_loader/` | flash 烧录时用的 helper | ❌ 仅 `make flash` 时用 |
| `partition/` | 临时 partition.bin | ⚠️ 中间产物 |
| `img_create/` | BLFlashCommand 的输出目录 | ⚠️ 临时输出 |
| `device_tree/`, `img_create_iot/`, `ota/` | 其他流程 | ❌ |

**大小**（at-v2.3.26）：
- `chips/bl602/` = 21 MB
- `chips/bl616/` = 20 MB
- `chips/bl616cl/` = 9.8 MB
- `chips/{bl618dg,bl702,bl702l}/` 加起来 ~80 MB（**at-project 不用**，可丢）

### C. 板级配置（`bsp/board/<board>/config/`）

每个 board 一份。`bflb_fw_post_proc` 通过 `--brdcfgdir` 读这个目录拿到：

| 文件 | 用途 | BL602 | BL616 | BL616CL |
|---|---|:---:|:---:|:---:|
| `partition_cfg_*.toml` | flash 分区表（单一真相） | ✅ `_4M.toml` | ✅ `_4M.toml` | ✅ `_4M.toml` |
| `boot2_*.bin` | bootloader prebuilt | ✅ `_release_v8.1.3.bin` | ✅ `_isp_release_v8.1.8.bin` | ✅ `_isp_release_v8.2.1.bin` |
| `mfg_*.bin` | manufacturing 固件 | ✅ `_gu_2e8b4aa47_40m_v2.90.bin` | ✅ `_gu_ef9d197c5_v2.50.bin` | ❌ 无 |
| `bl_factory_params_*.dts` | device tree | ✅ `_40M.dts` | ✅ `_auto.dts` | ✅ `_auto.dts` |

我们 driver-only 实验里已经验证过：这 4 类文件**位置可以通过 `FW_TOOL_CUSTOM_BOARD_DIR` GLOBAL property 重定向**。SDK 内部的 fallback 是 `${BL_SDK_BASE}/bsp/board/${BOARD}` —— 这个 fallback 也是当前抽出方案的痛点之一。

### D. Python 工具脚本（post_build 步骤用）

| 脚本 | 路径 | at-project 是否触发 |
|---|---|---|
| `unwind_6byte_table.py` | `tools/byai/` | ✅ BL616 (`CONFIG_BACKTRACE=y` by default) |
| `multi_bins.py` | `tools/byai/` | ✅ BL616 (CFI 拼接) |
| `gen_crt_bundle.py` | `components/crypto/mbedtls/bl_crt_bundle/` | ❌ `CONFIG_X509_CERTIFICATE_BUNDLE=n` |
| `partition_editor.py` | `tools/byai/` | ❌ 我们不用 |
| `coredump.py`、`crash_capture.py` | `tools/byai/` | ❌ 调试用 |
| `kconfig/menuconfig.py` | `tools/kconfig/` | ❌ 我们不跑 menuconfig |

**总体积**：`tools/byai/` = 148 KB（小，纯 Python）。

### E. CMake glue（`platform/bouffalo_sdk/cmake/`）

| 文件 | 内容 | 抽出难度 |
|---|---|---|
| `bflb_flash.cmake` | `combine` target + `post_build` target + 各种 `--key`/`--iv`/`--ota_ver` 处理 | 中等：220 行，逻辑明确 |
| `extension.cmake:270-360` | ELF→bin POST_BUILD step + SDK_LIBS 排序 + final link | **混在 SDK 整体 CMake 体系里**，抽出需要重写 link 模型 |
| `toolchain.cmake` | RISC-V cross-compiler 设定 | 简单 |
| `bouffalo_sdk-config.cmake` | SDK CMake package config，整个 `find_package(bouffalo_sdk)` 的入口 | **跟 SDK 整体绑死** |
| `auto_config.cmake`、`compiler_flags.cmake`、`footprint.cmake` | 杂项 | 不影响 ELF→bin |

## 三、at-project 实际用到的步骤（精简清单）

走 `proj.conf` 当前设置过一遍：

| 步骤 | 工具 | BL602 | BL616 | BL616CL |
|---|---|:---:|:---:|:---:|
| 1. ELF → raw .bin | `objcopy -Obinary` | ✅ | ✅ | ✅ |
| 2. bootheader + hash/ota/xz + 拷 boot2/partition/mfg/efusedata | `bflb_fw_post_proc` | ✅ | ✅ | ✅ |
| 3a. DWARF CFI .bin 生成 | `unwind_6byte_table.py` | ❌ | ✅ | ❌ |
| 3b. multi-bin 追加 DWARFCFI | `multi_bins.py` | ❌ | ✅ | ❌ |
| 3c. LP_FW patch | `patch_lpfw` | ❌ | ❌ | ❌ |
| 3d. ROMFS 生成 | `genromfs` | ❌ | ❌ | ❌ |
| 3e. cert bundle 生成 + 嵌入 | `gen_crt_bundle.py` + `multi_bins.py` | ❌ | ❌ | ❌ |
| 3f. dualcore NP/LP image append | `multi_bins.py` | ❌ | ❌ | ❌ |
| 4. 拼合 + pad 到 4 MB | `BLFlashCommand --build` + Python pad | ✅ | ✅ | ✅ |

**实际依赖的 SDK 资源**：
- 工具：`bflb_fw_post_proc` + `BLFlashCommand` + `unwind_6byte_table.py` + `multi_bins.py`
- 数据：`chips/{bl602,bl616,bl616cl}/`（仅这 3 颗）+ `bsp/board/<board>/config/`（仅这 3 个 board）
- CMake：`bflb_flash.cmake` 的 `combine` target 那一段，`extension.cmake` 的 POST_BUILD objcopy 步骤

`patch_lpfw`、`genromfs`、`gen_crt_bundle.py`、`mklfs` 等不在依赖链上，可丢（mklfs 是 `project/Makefile` 直接调，路径已经显式）。

## 四、抽出方案

### 阶段 0（最小可行）：复制 + 重写 build_whole.py

**目标**：让 `project/bouffalo/` 在 `platform/bouffalo_sdk/` 缺失时仍能从 ELF 生成 `whole_flash_data.bin`。

**目录布局提案**（`project/bouffalo/tools/`）：

```
project/bouffalo/tools/
├── bin/                                     # 平台二进制（PyInstaller 打包的）
│   ├── linux-x86_64/
│   │   ├── bflb_fw_post_proc                 (rename from -ubuntu)
│   │   └── BLFlashCommand
│   ├── macos/
│   │   ├── bflb_fw_post_proc
│   │   └── BLFlashCommand
│   └── windows/
│       ├── bflb_fw_post_proc.exe
│       └── BLFlashCommand.exe
│
├── chips/                                   # BLFlashCommand 数据库（只保留我们用的 3 颗）
│   ├── bl602/
│   ├── bl616/
│   └── bl616cl/
│       └── (efuse_bootheader/, partition/, img_create/, ...)
│
├── byai/                                    # Python 脚本（post_build 用）
│   ├── multi_bins.py
│   └── unwind_6byte_table.py
│
├── board_configs/                           # 板级 config（含 boot2/mfg prebuilt + dts + partition.toml）
│   ├── bl602dk/
│   ├── bl616dk/
│   └── bl616cldk/
│
├── cmake/                                   # 取代 SDK bflb_flash.cmake 的最小子集
│   ├── bflb_flash.cmake                     # combine + post_build target 的精简版
│   └── toolchain.cmake                      # RISC-V cross-compiler 设定
│
├── build_whole.py                           # （已存在）拼合 + pad 到 4 MB
├── postproc_elf.py                          # 新：wrap objcopy + bflb_fw_post_proc + multi_bins
└── pick_tool.py                             # 新：按 host OS 选 bin/<platform>/ 下的二进制
```

**估计大小**：
- 3 颗芯片 chips/ ≈ 51 MB
- 工具 binary（3 平台 × 2 工具）≈ 50-60 MB
- board_configs / byai / cmake / scripts ≈ 数百 KB
- **总计 ~110-120 MB** 入 git（必须 lfs 或下游 fetch 脚本，不能直接 commit binary）

**风险点 / 待决定**：

1. **PyInstaller binary 怎么入仓**？直接 git add 大文件（CI 检 LFS 配置），还是写 fetch 脚本运行时从 SDK 拷？建议 fetch 脚本（避免污染 git history）。
2. **板级 config (boot2/mfg) 是否完整搬过来**？这些是 vendor prebuilt blob，跟 SDK 版本绑定。如果将来 bump SDK，要同步更新。**建议跟 SDK 同 git tag 锁定**。
3. **BLFlashCommand 内部对 `chips/<chip>/` 是相对路径还是绝对路径访问**？我之前看到 `chips/<chip>/img_create/whole_flash_data.bin` 是工具相对自身位置访问的，所以 chips/ 必须紧贴工具二进制（同目录或上级）。**需要 verify**——脚本第一步就要测。
4. **CMake `extension.cmake` 里 ELF→bin POST_BUILD 步骤** 是 SDK link 流程内的一行 `add_custom_command(... POST_BUILD ... objcopy)`。要让它**独立于 SDK** 意味着要么 fork CMake 体系（大工程），要么在 `project/bouffalo/CMakeLists.txt` 之外再加一层 wrap（用 Make rule 跑 objcopy）。
5. **bflb_fw_post_proc 内部读 `chips/<chip>/efuse_bootheader/efuse_bootheader_cfg.conf`** 是否也按工具相对位置找？同 #3，需 verify。

### 阶段 1（理想态）：重写 bflb_fw_post_proc 为开源 Python

`bflb_fw_post_proc` 和 `BLFlashCommand` 都是 Bouffalo 用 PyInstaller 打包的闭源 Python 脚本（社区有人逆向但不完整）。如果想完全脱钩，需要**从 toml partition 表 + boot2 + bootheader 模板 + ELF 自己**生成最终 binary。工作量很大，相当于重新实现 vendor flasher。

**结论**：阶段 0 是务实选项。阶段 1 是长期目标，无短期 ROI。

## 五、抽出后的不变量（验证清单）

切换前后必须保持比特一致：

```bash
# 切换前
make -C project bl616
md5sum project/bouffalo/build_bl616/build_out/whole_flash_data.bin
# 切换后（不依赖 platform/bouffalo_sdk/）
make -C project bl616
md5sum project/bouffalo/build_bl616/build_out/whole_flash_data.bin
# 两者应该 byte-by-byte 相同
```

每个 chip 都要验证。BL616 还要验 DWARF CFI 段位置（因为 `CONFIG_BACKTRACE=y` 多走一步 `multi_bins.py append DWARFCFI`）。

## 六、风险提示

1. **vendor binary 兼容性**：bflb_fw_post_proc + BLFlashCommand 跟 chips/<chip>/ 数据库版本耦合（bootheader 模板、partition 解析格式可能跨版本不兼容）。**抽出版本要锁死成套**。
2. **glibc 版本**：`bflb_fw_post_proc-ubuntu` 是 dynamic ELF for "GNU/Linux 2.6.32"（即 glibc≥2.5）— 但 PyInstaller 实际可能 require glibc 更高版本。CI/macOS runner 上要测。
3. **`extension.cmake` 的 ELF link 流程**：SDK link 时用 `-Wl,--whole-archive ${SDK_LIBS}`，如果不 fork CMake，ELF 的生成还是绑 SDK；本调查只覆盖**ELF→bin 之后**的阶段，**ELF 本身的生成仍依赖 SDK**。
4. **三颗芯片差异**：BL602 用 `--brdcfgdir` 指向不同 dts；BL616CL 没 mfg；BL616 有 DWARF CFI append。抽出脚本要按 chip 分支处理。

---

## 附录 A：关键 SDK 路径速查

```
platform/bouffalo_sdk/
├── cmake/
│   ├── bflb_flash.cmake                                # combine + post_build target
│   ├── extension.cmake                                 # 270-360 行：POST_BUILD objcopy
│   └── toolchain.cmake                                 # CMAKE_OBJCOPY 等
│
├── tools/
│   ├── bflb_tools/
│   │   ├── bflb_fw_post_proc/
│   │   │   ├── bflb_fw_post_proc-{ubuntu,macos,exe}    # PyInstaller binary
│   │   │   └── usage.txt
│   │   └── bouffalo_flash_cube/
│   │       ├── BLFlashCommand-{ubuntu,macos,exe,arm,freebsd}
│   │       ├── chips/<chip>/efuse_bootheader/...
│   │       └── chips/<chip>/img_create/                # tool 输出落点
│   ├── byai/
│   │   ├── unwind_6byte_table.py                       # DWARF CFI extractor
│   │   └── multi_bins.py                               # binary appender
│   ├── lpfw/patch_lpfw-{ubuntu,macos,exe}              # at-project 不用
│   ├── genlfs/mklfs-{ubuntu,macos,exe}                 # 已经独立调用
│   └── genromfs/                                       # at-project 不用
│
└── bsp/board/<board>/config/
    ├── partition_cfg_*.toml                            # 分区表
    ├── boot2_*.bin                                     # bootloader prebuilt
    ├── mfg_*.bin                                       # 可选
    └── bl_factory_params_*.dts                         # device tree

project/bouffalo/
├── Makefile                                            # CHIP=xxx 入口
├── CMakeLists.txt                                      # find_package(bouffalo_sdk) 引入
├── proj.conf                                           # 已无 POST_BUILDS_*
└── tools/build_whole.py                                # （已存在）BLFlashCommand wrapper
```

## 附录 B：build_out/<chip>/ 实际产物分类

| 产物 | 来源 | 是否进 whole_flash_data.bin |
|---|---|:---:|
| `at_<chip>.elf` | CMake link | ❌（开发用） |
| `at_<chip>.map` | linker `-Wl,-Map=` | ❌ |
| `at_<chip>.bin` | objcopy + bflb_fw_post_proc 加 bootheader | ✅ 主固件 |
| `at_<chip>.bin.hash` | bflb_fw_post_proc | ❌（OTA 校验用） |
| `at_<chip>.bin.ota` | bflb_fw_post_proc | ❌（OTA 包） |
| `at_<chip>.xz` / `.xz.hash` / `.xz.ota` | bflb_fw_post_proc | ❌（压缩 OTA） |
| `boot2_*.bin` | 从 `bsp/board/<board>/config/` 拷 | ✅ bootloader |
| `mfg_*.bin` | 从 `bsp/board/<board>/config/` 拷（BL616CL 无） | ✅ |
| `partition.bin` | bflb_fw_post_proc 从 `.toml` 生成 | ✅ |
| `efusedata*.bin` | bflb_fw_post_proc | ❌（efuse 编程用） |
| `whole_flash_data.bin` | `build_whole.py` 拼合上述 + pad 4 MB | ✅ 最终 |
