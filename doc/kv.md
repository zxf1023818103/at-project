# KV Storage on LittleFS

三方 SDK 都用 littlefs 当 KV 后端。本文档记录跨平台共用 LFS 镜像所需的约定 ——
**Bouffalo 烧的 LFS 区，原样拷到 Ameba 板子上要能读出同样 KV**。

## 镜像里的路径布局

| 平台 | KV 入口 API | LFS 上的路径 |
|---|---|---|
| Bouffalo BL602 / BL616 / BL616CL | `ef_get_env_blob` / `ef_set_env_blob` | `/KV/<key>` |
| Ameba D RTL8721D / RTL8710C | `rt_kv_get` / `rt_kv_set` / `rt_kv_delete` | `/KV/<key>` |
| Ameba Dplus RTL8711Dx, Green2 RTL8711F | `rt_kv_get` / `rt_kv_set` / `rt_kv_delete`（走 VFS）| `/KV/<key>` |

新 Ameba 在用户层路径里多一个 VFS 挂载点前缀（`<region>:KV/<key>`），但前缀只
解析挂载点、不写入 lfs；lfs 镜像里看到的仍是 `/KV/<key>`。

## 文件内容

每个 key 对应一个 lfs 文件，文件名等于 key，**内容是 raw value，没有 header
也没有 CRC** —— 三方都靠 littlefs 自身的 commit / CRC 来保证完整性。

> Ameba 老版 `rt_kv_set` 支持通过 `kv_encrypt` 回调把 value AES 加密再写盘
> （`platform/ameba-rtos-d/component/common/file_system/kv/kv.c:43-56`）。
> **这条路径与跨平台共用不兼容，禁用**：保持 `kv_encrypt == NULL`（默认）。

## Key 命名约束

Bouffalo 的 `lfs_easyflash.c` 在写文件名时会做转义：

- `/` (0x2F) → `}` + `0x0F`
- `}` (0x7D) → `}]`

Ameba 两边的 `kv.c` 不做转义，直接 `sprintf("KV/%s", key)`。

→ **key 不要含 `/` 和 `}`**。否则 Bouffalo 写的文件 Ameba 找不到（反之亦然）。
其他可打印 ASCII 字符没问题。

## 镜像跨平台前提

- 三方 littlefs 源码一致：v2.8.0 + Realtek `LFS_ERR_NOTFMT` 补丁，disk version
  2.1（参见前次提交记录）
- 三方 lfs config 一致：`block_size=4096`, `read_size=prog_size=256`,
  `cache_size=512`, `lookahead_size=256`, `block_cycles=500`
- LFS 分区大小一致（默认 1 MB → 256 blocks）—— 参见 [flash_layout.md](flash_layout.md)
- 不启用 `kv_encrypt`（默认就关）
- KV key 满足上面的命名约束

满足以上五条，BL602 上 `ef_set_env_blob("hello", "world", 5)` 写出来的 lfs
区，可以直接刷到 Ameba Dplus 对应分区，启动后 `rt_kv_get("hello", buf, 5)`
能读到 `"world"`。

## 历史 KV 数据迁移（升级 SDK 时）

升级前 Bouffalo 用的命名空间是 `/_ef4_kvs_`；升级后是 `/KV`。LFS 镜像里旧目录
会保留但新代码看不到内容 —— 等同于"丢失"。

当前 src/main.cpp 还没有任何 `ef_*` / `rt_kv_*` 实际调用，所以现网设备里 KV
区都是空的，无需迁移。**等真要用 KV 时，再决定要不要写一次性迁移代码**
（mount → 遍历 `/_ef4_kvs_/` → 复制到 `/KV/` → 删除旧目录）。
