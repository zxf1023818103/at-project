# LittleFS 镜像根

`make -C project lfs_image` 会用 [mklfs](../../doc/lfs.md) 把这个目录的全部内容
打包成 `project/lfs.bin`，再由各平台合成固件流程烧到 raw offset `0x300000`
开始的 LFS 区。

目录里的每个文件 / 子目录都会原样进入 lfs 文件系统的根目录。

## KV 预填

`resource/KV/<key>` 这个相对路径直接对应运行时：

| 平台 | 读取 API |
|---|---|
| Bouffalo BL602 / BL616 / BL616CL | `ef_get_env_blob("<key>", buf, sizeof(buf), &got)` |
| Ameba D / Dplus / Green2 | `rt_kv_get("<key>", buf, sizeof(buf))` |

文件内容**就是 value**，没有 header 也没有 CRC（保护由 littlefs 兜底）。

key 命名约束：不要含 `/` 或 `}` —— 见 [../../doc/kv.md](../../doc/kv.md)。
