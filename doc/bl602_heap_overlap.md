# BL602 heap layout：lwip `ram_heap` 跟 mm OCRAM 池物理重叠（latent bug）

记一个还没炸但**一定会炸**的 RAM 冲突。当前能跑只是因为 mm 池总用量没超过冲突点。

## TL;DR

BL602 上跑着 **三套独立的动态分配器**，且其中两套的 backing 区间**物理上覆盖第三套**：

| 分配器 | 入口 | backing 区间 | 大小 |
|---|---|---|---|
| FreeRTOS heap_4 | `pvPortMalloc` | `ucHeap[]` (`.bss`) | `configTOTAL_HEAP_SIZE` = 60 KB |
| mm 子系统 (TLSF) | `kmalloc` ← `_malloc_r` (newlib) ← `malloc` / `new` | `__HeapBase` → `__HeapLimit` | ~79 KB |
| lwip 内部 heap | `mem_malloc` (lwip 自己的 allocator) | `ram_heap[]` (`.wifibss`) | 8 KB (=`MEM_SIZE`) |

**问题**：`ram_heap` 的物理地址区间 **完全落在 mm OCRAM 池区间里**。mm TLSF 不知道这块被 lwip 占了，迟早会从这里给用户分配出去，跟 lwip 撞车。

## 具体地址（当前 build，BL602 + experiment/driver-only）

```
0x42020000  __StackTop         (system stack 起点)
0x42020ec8  __bss_start__
0x42027288  ucHeap[]           = 60 KB    (FreeRTOS heap_4, in .bss)
0x42036288  ucHeap end
0x42037f34  __bss_end__
0x420382c0  __HeapBase         (.heap section, mm OCRAM 池起点)
            │
            │   mm TLSF 看到的 free 区间
            │   (79 KB = __HeapLimit - __HeapBase)
            │
0x42045378  ram_heap[]         = 8 KB     (lwip mem_malloc, in .wifibss)
0x4204738b  ram_heap end       ← mm 不知道这段被占了！
            │
            │   mm TLSF 继续往后是它以为的 free 区间
            │
0x4204c000  __HeapLimit        = ORIGIN(ram_memory) + 176 KB
```

mm `__HeapBase` 到 `ram_heap` start 之间的距离：

```
0x42045378 - 0x420382c0 = 0xd0b8 = 53,432 字节 ≈ 52 KB
```

只要 mm TLSF 单次或累积分配的最高点超过 `__HeapBase + 52 KB`，就会拿到 `ram_heap` 区域里的地址，然后 user 跟 lwip 互相踩内存。

## 为什么 ld 没报 overlap

`bl602_flash.ld` 把同一片物理 SRAM (0x42020000 - 0x4204c000) 声明成两个**重叠的 region**：

```
ram_memory  (!rx) : ORIGIN = 0x42020000,                     LENGTH = 176K
ram_wifi    (wxa) : ORIGIN = 0x42042000 - __EM_SIZE,         LENGTH = 40K  (≈ 0x42042000 - 0x4204a000)
```

GNU ld 允许 region overlap — 它只保证 **每个 section 落在自己声明的 region 内**，不保证不同 region 内的 section 之间不重合。`.bss` / `.heap` 走 `> ram_memory`，`.wifibss` 走 `> ram_wifi`。lwip 的 `ram_heap` 是 `.wifibss`，落在 ram_wifi 子段；mm 的 `__HeapBase..__HeapLimit` 是 ram_memory 顶段。两个段在物理 RAM 上重叠，但都被 ld 满足。

更具体地，`__HeapLimit` 在 ld 里被赋值两次：

```
.heap (NOLOAD): {
    __HeapBase = .;
    ...
    __HeapLimit = .;   /* heap 段末尾 — 通常就是 . */
} > ram_memory
__HeapLimit = ORIGIN(ram_memory) + LENGTH(ram_memory);  /* 又被覆盖成 RAM 顶 */
```

后一行无条件把 `__HeapLimit` 拉到 RAM 顶 — 这就把 `ram_wifi` 区间整个并入了 mm 池的"可用"区间。

## 为什么现在没炸

今天 BL602 bring-up 跑到 wifi idle + AT loop 时，mm 池实际用量大概在 20-30 KB 量级（一堆 newlib bootstrap + 一些 kmalloc）。距离冲突点 52 KB 还有差距。

但任何一个会大量 `malloc` 的 use case（zlib `compress()` 默认 256K 之所以早炸，是因为单次请求就超过整池；TLS handshake / 大 JSON / mbedtls big int 操作累积都能慢慢爬上去）都会撞上 `ram_heap`，**症状会是 lwip 莫名其妙乱跑 / TCP 收发卡死 / mem_malloc 返回错误指针**，非常难调。

## 怎么修

按"侵入度"从轻到重：

### 方案 A — 让 lwip 的 mem_malloc 走 mm 池（推荐）

把 lwipopts.h 里加：
```c
#define MEM_LIBC_MALLOC  1
```
开了以后 lwip 内部用 `mem_malloc` / `mem_free` 直接走 `malloc` / `free`（即走 mm）。

- ✅ 消除 `ram_heap`，省 8 KB BSS
- ✅ lwip 跟 user code 共用一个池，碎片化在一起
- ⚠️ 短时大量 socket buffer 分配会跟 user malloc 争 mm 池配额
- ⚠️ 上游 lwip 文档里 `MEM_LIBC_MALLOC` 标 "use with caution"，因为 IRQ 上下文可能调 `pbuf_free` → `mem_free`，而 newlib malloc 不是 IRQ-safe。需要确认 BL602 build 不在 IRQ 里 free pbuf；如果有，需要包一层 critical section。

### 方案 B — 让 `__HeapLimit` 避开 ram_wifi region

改 `bl602_flash.ld`，把 `.heap` 段从 RAM 顶下沉到 `ram_wifi` 之前：

```
__HeapLimit = ORIGIN(ram_wifi);   /* 而不是 ORIGIN(ram_memory) + LENGTH(...) */
```

这样 mm 池从 ~79 KB 缩到约 40 KB（取决于 `__EM_SIZE`），换取没有 overlap。

- ✅ 物理上根治
- ⚠️ mm 池腰斩，要重新算给 newlib/C++/user 用够不够（之前 24 KB FreeRTOS heap 都不够，mm 这边也得保留够大）

### 方案 C — 让 FreeRTOS 也走 mm

写一个 `pvPortMalloc` shim wrap `kmalloc`，把 `heap_4.c` 从 archive 挤掉。这能消除 heap_4 + mm 的双池割裂（之前 `vApplicationMallocFailedHook` 在 24 K heap_4 满了时炸，那一刻 mm 还有 80+ K 空闲，就是这种割裂的代价），但不直接解决 mm vs ram_heap 冲突 — 还得配合 A 或 B。

可以跟 A 组合：A+C 之后整个 firmware 只有一个 mm 池，最干净。

## 邻接问题：BL616CL/BL616 是不是也有

只 BL602 / BL702 / BL702L 这类**单 SRAM bank**的 chip 受影响。BL616CL 的 ld 把 `ram_memory` 放在 `0x60FE0000`，`ram_wifi` 放在 `0x20xxxxxx` —— 物理上是两块不同的 SRAM bank，互不重叠。BL616 待确认（没找到本地 ld，可能在 SDK 里）。

## 验证脚本

`ram_heap` 是否落在 `[__HeapBase, __HeapLimit)`，构建产物里 grep 一下 nm 输出就能查：

```sh
NM=platform/toolchain_gcc_t-head_linux/bin/riscv64-unknown-elf-nm
$NM project/bouffalo/build_bl602/at_bl602.elf | \
  grep -E ' [BbDd?] (__HeapBase|__HeapLimit|ram_heap)\b'
```

如果 `ram_heap` 的地址落在 `__HeapBase..__HeapLimit` 区间内 → 有 overlap。
