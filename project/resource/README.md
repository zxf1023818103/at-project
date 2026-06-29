# LittleFS image root

`make -C project lfs_image` packs everything under this directory into
`project/lfs.bin` (via [mklfs](../../doc/lfs.md)), which each platform's firmware
assembly step then flashes to the LFS region starting at raw offset `0x300000`.

Every file / subdirectory here lands verbatim at the root of the lfs filesystem.

## KV pre-seed

The relative path `resource/KV/<key>` maps directly to the runtime KV store:

| Platform | Read API |
|---|---|
| Bouffalo BL602 / BL616 / BL616CL | `ef_get_env_blob("<key>", buf, sizeof(buf), &got)` |
| Ameba D / Dplus / Green2 | `rt_kv_get("<key>", buf, sizeof(buf))` |

The file content **is the value** - no header, no CRC (integrity is left to
littlefs).

Key naming constraint: no `/` or `}` - see [../../doc/kv.md](../../doc/kv.md).
