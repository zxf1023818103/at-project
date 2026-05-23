---
name: project-lfs-image-pipeline
description: project/lfs.bin 是怎么一份镜像跨三方平台通用的（mklfs 参数、嵌入点、Ameba job 不拉 bouffalo_sdk 的取舍）
metadata: 
  node_type: memory
  type: project
  originSessionId: 35e665c4-7ffc-49e2-a5a5-a95c39a3b51e
---

`project/lfs.bin` 是跨平台共用的 LittleFS 预填镜像，烧机后立即能从 `KV/*`
读出（Bouffalo: `ef_get_env_blob`，Ameba: `rt_kv_get`）。三方共用的前提：

- LFS 区起始 raw offset 都是 `0x300000`，大小都是 **1000 KB**（`0xFA000`）—
  对齐 Ameba D 的 4 MB layout（D 的 settings sector 在 LFS 后面，没法扩到
  1024 KB）。三方 SDK 里都改过：[[reference-flash-layout]] 的更新版本。
- littlefs 源码三方完全一致（v2.8.0 + Realtek `LFS_ERR_NOTFMT` 补丁）
- mklfs 命令模板：`mklfs-ubuntu -c <dir> -b 4096 -r 256 -p 256 -s 1024000 -i <out>`
- `cache_size` / `lookahead_size` 不写盘，所以 mklfs 自动算的值跟运行时
  `cache_size=512` / `lookahead_size=256` 不冲突

**Why**：把"逻辑互通"变成可执行的工程链路 —— 一份镜像，一份预填，6 个平台通烧。
之前每个 SDK 自带不同的 KV 目录名（bouffalo `/_ef4_kvs_` vs ameba `/KV`），先
通过改 bouffalo 那边的 `LFS_EF_NAMESPACE` 宏统一到 `/KV`，再做这一步的镜像
打包。

**How to apply**：

- 改 `project/resource/KV/<key>` 文件加预填 KV → 跑 `make -C project <plat>`
  即可触发 lfs.bin 重生成并嵌入合成固件；不需要单独跑 lfs_image
- 顶层 Makefile 7 个 target：`lfs_image / bl602 / bl616 / bl616cl / amebad /
  amebadplus / amebagreen2`，前 6 个 platform target 都依赖 `lfs.bin`
- 嵌入机制：
  - Bouffalo 走 BLFlashCommand 原生 partition：`build_whole.py` 给 ini 加
    `[PSM]` 段，地址 `@partition`
  - Ameba 走 Makefile 里的 `dd`：基底文件 `count=0x3FA000`，然后
    `dd if=../lfs.bin seek=0x300000`
- CI（`.github/workflows/build.yml`）特殊处理：单独一个 `lfs_image` job 只 init
  bouffalo_sdk 跑 mklfs 上传 artifact；6 个 build job 用 `needs: lfs_image` +
  `download-artifact` 拿到 lfs.bin。**Ameba job 不 init bouffalo_sdk** —— 否则
  每个 ameba job 多 1.2 GB clone

**验证**：cmp 比对最终固件 0x300000 起 1000 KB 跟 lfs.bin 是否字节一致：
```sh
LFSSZ=$(stat -c %s project/lfs.bin)
cmp -n $LFSSZ project/lfs.bin <(dd if=<merged.bin> bs=1 skip=$((0x300000)) count=$LFSSZ status=none)
```

**陷阱**：
- LFS disk geometry 变更（之前 1024 → 1000 KB）会让现网 lfs 重格式化，老
  KV 全丢。当前 src/main.cpp 没真业务在写 KV，所以无所谓；将来如果要保留
  历史数据，需要在 mount 之前先做迁移
- TestTask::testFileSystem1() 没挂入 routine()，要手动调一次才能在板子上
  跑自检
- mklfs 二进制只在 bouffalo_sdk/tools/genlfs/ 里（80 KB binary，三平台都有），
  Ameba SDK 没自带；本地全平台 build 没问题（bouffalo_sdk 是 submodule），
  CI 要走 artifact 分发
