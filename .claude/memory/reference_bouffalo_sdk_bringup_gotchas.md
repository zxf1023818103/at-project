---
name: reference-bouffalo-sdk-bringup-gotchas
description: BL602 first-boot bring-up 踩到的 5 个 bouffalo_sdk 集成 gotchas，BL616/BL616CL/BL702/BL702L 后续 bring-up 大概率同病可直接套
metadata: 
  node_type: memory
  type: reference
  originSessionId: f0590ad3-f088-463f-bcec-f9d6a043637c
---

`experiment/driver-only` branch 上 BL602 第一次上板时踩了一组 SDK 集成 gotcha，全部是 bouffalo_sdk 默认 wiring 跟 wifi+BT builds 实际需求脱节。BL702 / BL702L (single-SRAM-bank chips, 同 freertos_port 同 mm) 和 BL616 / BL616CL (runtime 未验证) 后续 bring-up 直接对照下面这几条排查，比从 panic 反推快得多。

具体地址、症状、commit hash 见 doc/ + git log；这条 memory 只列出"该看哪几个文件 + 已知陷阱方向"。

## 1. bflb_freertos_port FreeRTOSConfig.h 是 demo defaults

`component/bouffalo_sdk/bflb_freertos_port/include/FreeRTOSConfig.h` 默认给的是 FreeRTOS demo 值（`configMAX_PRIORITIES=7`、`configTOTAL_HEAP_SIZE=24K`、`configTIMER_TASK_STACK_DEPTH=configMINIMAL_STACK_SIZE=128 words`）。wifi driver 创 task 用 priority 30，BT hci 用 27 / 15；wifi+lwip+BT init 24K heap 不够；timer task 默认 stack 在 wifi mgmr callback 里溢出。

对照参考：`platform/bouffalo_sdk/examples/wifi/sta/*/FreeRTOSConfig.h`（这些值是 SDK 实际验证过的）。typical wifi build 需要 `configMAX_PRIORITIES=32`、heap `60K` 起步（BL602 RAM 不够 100K，BL616+ 可以 100K）、timer stack 1024 words。

## 2. newlib `_malloc_r` 走 `ecall SYS_brk`，裸机会死

`platform/bouffalo_sdk/components/libc/newlib/port_memory.c` 把 `_malloc_r` / `_free_r` / `_sbrk_r` 等 retarget 到 `kmalloc()`（走 mm 子系统）。但 `port_memory.c.obj` 是 archive 成员，没人显式引用就不被 pull —— newlib 自己的 syscall-flavor `_malloc_r` 赢了链接顺序，发 `ecall SYS_brk` 期待 OS 接住，裸机会让 ecall 落到 exception_entry，heap_end=0，第一次 malloc 写 NULL 炸。

修法：在 exe 的 link options 加 `-Wl,-u,_malloc_r -Wl,-u,_free_r -Wl,-u,_calloc_r -Wl,-u,_realloc_r -Wl,-u,_memalign_r -Wl,-u,_sbrk_r -Wl,-u,_sbrk` 强制 archive pull。注意：把 `-u` 加在 `bflb_libc` 的 `sdk_add_link_options` 里**不生效**（CMake 在那个 INTERFACE library 上行为怪），必须加到 exe target 的 `target_link_options(at PRIVATE ...)` 上。

## 3. `littlefs` PUBLIC compile defs 要挂在 littlefs target 自己上

`LFS_THREADSAFE` 决定 `struct lfs_config` 有没有 lock/unlock 两个 fn-ptr（layout 差 8 字节）。`bflb_littlefs_port` 用 `target_compile_definitions(... PUBLIC LFS_THREADSAFE)` 给 consumer (lfs_easyflash)，但 PUBLIC 不回传到自己 link 的 dep —— `littlefs` lib 编 `lfs.c` 时不带 THREADSAFE，跟 `lfs_easyflash.c` 的 layout 错位，静态初始化的字段被读成 lookup 错偏移，store fault。

修法：把 `target_compile_definitions(littlefs PUBLIC LFS_THREADSAFE)` 挂在 `cmake/littlefs.cmake` 的 `littlefs` target 本身上，保证 lib 内 source 和所有 consumer 一致。

## 4. single-SRAM-bank chips: lwip `ram_heap` 跟 mm OCRAM 池物理重叠

BL602 / BL702 / BL702L 这类只有一片 SRAM 的 chip，`<chip>_flash.ld` 把同一片物理 RAM 声明成两个重叠的 region（`ram_memory` 和 `ram_wifi`）。lwip `ram_heap[]` 在 `.wifibss` → ram_wifi，物理上落在 mm OCRAM 池 `[__HeapBase, __HeapLimit)` 区间里。mm TLSF 不知道这段被占，分配累积超过 ~52 KB 就撞 lwip 写过的字节。当前不炸只是因为用量没到，长期跑 / 大块 malloc / 碎片化场景会触发。

修法：在 `bflb_lwip_port/config/lwipopts.h` 加 `MEM_LIBC_MALLOC=1`，让 lwip mem_malloc 走 libc malloc → port_memory.c → kmalloc → 同 mm 池。pbuf_free 在 tcpip task 里调，不在 ISR，所以 libc malloc 非 IRQ-safe 在这条 path 上不是问题。详细见 `doc/bl602_heap_overlap.md`。

BL616CL 因为 `ram_memory` 在 0x60xxxxxx / `ram_wifi` 在 0x20xxxxxx 不同物理 bank 不受影响；BL616 待 ld 确认。

## 5. mm (kmalloc) 跟 FreeRTOS heap_4 (pvPortMalloc) 是分离的双池

bouffalo_sdk 设计上 `pvPortMalloc` 走 FreeRTOS `heap_4.c`（`ucHeap[configTOTAL_HEAP_SIZE]` static array），`malloc` / `new` 经 `_malloc_r` 走 mm 子系统的 OCRAM 池 (`mm_register_heap(MM_HEAP_OCRAM_0, "OCRAM", MM_ALLOCATOR_TLSF, &__HeapBase, ...)`)。两个池不互通：FreeRTOS task/queue 创建吃 ucHeap，user malloc/C++ new 吃 mm。前者满后者不能借（看到 `vApplicationMallocFailedHook` 时 mm 那边可能还有几十 KB 空闲）。

加 #4 的 `MEM_LIBC_MALLOC=1` 之后 lwip 也并入了 mm 池，剩下 FreeRTOS heap_4 还是独立。要真正一个池，得写个 `pvPortMalloc` shim wrap `kmalloc` 把 `heap_4.c.obj` 从 archive 挤掉。目前没做，因为 60K heap_4 + 79K mm 当前够用，且 user 写 `pvPortMalloc` shim 会把 SDK 的 heap_4 archive member dead-strip 行为打乱，需要仔细测。

## 相关文件 / 入口

- `component/bouffalo_sdk/bflb_freertos_port/include/FreeRTOSConfig.h` — #1 改这里
- `project/bouffalo/CMakeLists.txt` — #2 `target_link_options(at PRIVATE -Wl,-u,_malloc_r ...)` 加在这里
- `cmake/littlefs.cmake` — #3 `target_compile_definitions(littlefs PUBLIC LFS_THREADSAFE)`
- `component/bouffalo_sdk/bflb_lwip_port/config/lwipopts.h` — #4 `MEM_LIBC_MALLOC=1`
- `doc/bl602_heap_overlap.md` — #4 详细分析 + 三个修法对比
- `doc/component_migration_status.md` "下次继续要看的" — 后续 bring-up 待办列表
- `platform/bouffalo_sdk/examples/wifi/sta/*/FreeRTOSConfig.h` — #1 SDK 验证过的参考值
- `platform/bouffalo_sdk/components/libc/newlib/port_memory.c` — #2 看 SDK 怎么 retarget newlib

## 跟其他 chip 对照

- BL702 / BL702L — single bank chip，几乎肯定踩 #4；其他可能也踩，验证时按 1-5 顺序排查
- BL616 / BL616CL — 多 bank，#4 大概率不踩；#1/#2/#3/#5 仍然适用因为同 SDK 同 freertos_port
- Ameba D / D+ / Lite / Smart / Green2 — 完全不同 SDK，这些 gotcha 不适用，见 [[reference-amebad-flash-layout]] 等 Ameba 系列 memory
