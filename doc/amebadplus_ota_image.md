# Ameba Dplus / Green2 `ota_all.bin` 生成流程

针对 `platform/ameba-rtos`（新 SDK）下 amebadplus / amebagreen2 的 OTA 升级镜像 `ota_all.bin` 是怎么从 KM0 + KM4 两核的中间产物拼出来的。Green2 流程基本一致，只是核改名 `km4tz/km4ns`，下面以 amebadplus 为主线。

# 最终产物

`project/ameba/build_RTL8711Dx/ota_all.bin` —— 用本工程当前配置（`CONFIG_TRUSTZONE=n`、`CONFIG_UPGRADE_BOOTLOADER=n`、`CONFIG_COMPRESS_OTA_IMG=n`）实测：

```
km0_km4_app.bin   1,106,912 B
ota_all.bin       1,106,944 B   (= km0_km4_app.bin + 32 B 的 OTA 头)
```

# 三个核心步骤

```
              per-image build                  cross-image stitch
┌─────────────────────────────────┐    ┌─────────────────────────────────┐
│ project_km0 image2 postbuild    │    │ component/.../postbuild.cmake   │
│   → km0_image2_all.bin          │ ─┐ │                                 │
│                                 │   ├▶│ fw_pack:                       │
│ project_km4 image2 postbuild    │   │ │   km0_image2_all.bin +         │
│   → km4_image2_all.bin          │ ─┘ │   km4_image2_all.bin            │
│                                 │    │   → km0_km4_app.bin             │
│ project_km4 image1 postbuild    │    │                                 │
│   → km4_boot_all.bin            │ ─▶ │ ota_prepend_head:               │
│                                 │    │   (km4_boot_all.bin if          │
└─────────────────────────────────┘    │    CONFIG_UPGRADE_BOOTLOADER)   │
                                       │   + km0_km4_app.bin             │
                                       │   → ota_all.bin                 │
                                       │                                 │
                                       │ copy → FINAL_IMAGE_DIR          │
                                       └─────────────────────────────────┘
```

## 1) 单核 image2 → `kmX_image2_all.bin`

各核 `image2/CMakeLists.txt` 在 image2.axf 链接好后，自己跑一段 postbuild（`-P .../postbuild.cmake`，由 `c_POST_BUILD_SCRIPT` 决定，默认 `postbuild.cmake`）。这一步把 `.axf` 切片成需要烧到 flash 的 sram/xip 段 + 拼成 `km0_image2_all.bin` / `km4_image2_all.bin`。这一步出来的就是单核可加载的镜像。

## 2) `ameba_firmware_package`：fw_pack 拼成 `km0_km4_app.bin`

`platform/ameba-rtos/component/soc/amebadplus/project/CMakeLists.txt:48-54` 把 `km0`/`km4` 喂给：

```cmake
ameba_firmware_package(
    ${app_name}                # km0_km4_app.bin（普通构建）或 km0_km4_app_mp.bin（MP 构建）
    p_MCU_PROJECTS km0 km4    # 顺序决定 image2/image3 在最终二进制里的顺序
)
```

定义见 `platform/ameba-rtos/cmake/common.cmake:357-424`：

- 收集每个 mcu_proj 的 `image1_all`、`image2_all`、`image3_all` 文件，传成 list 到 cmake `-D` 变量（`c_IMAGE1_ALL_FILES` / `c_IMAGE2_ALL_FILES` / `c_IMAGE3_ALL_FILES`）。
- 注册 `firmware_package` 这个 ALL 自定义目标，依赖所有 per-image 的 `_postbuild` 目标，在它们都跑完之后用 `cmake -P` 调一次 SOC 级别的 `postbuild.cmake`。

也就是说：`ameba.py build RTL8711Dx` 跑到第 833/834 时（搜 build log 里的 `Image app generate start`）就是这一步。

SOC 级 postbuild 头一件事是把 `km0/km4` 两个 image2_all 喂给 `ameba_axf2bin_fw_pack` 拼出 `km0_km4_app.bin`：

`platform/ameba-rtos/component/soc/amebadplus/project/postbuild.cmake:18-29`

```cmake
ameba_axf2bin_fw_pack(
    ${app_full_path}                    # = ${c_IMAGE_OUTPUT_DIR}/km0_km4_app.bin
    p_IMAGE2 ${c_IMAGE2_ALL_FILES}      # [km0_image2_all.bin, km4_image2_all.bin]
    # TrustZone 才有：
    # p_IMAGE3 ${c_IMAGE3_ALL_FILES}
)
```

`ameba_axf2bin_fw_pack` 见 `platform/ameba-rtos/cmake/utility_base.cmake:1209`，最终调到 python：`tools/scripts/axf2bin.py fw_pack` → `image_process/op_firmware_package.py`。

之后是几个可选的处理（本工程都没开）：

- `CONFIG_FATFS_WITHIN_APP_IMG`：把 `fatfs.bin` 4 KB 对齐然后 cat 到 `km0_km4_app.bin` 末尾。
- `CONFIG_COMPRESS_OTA_IMG`：用 `ameba_axf2bin_compress` 把 `km0_km4_app.bin` 压缩成 `_compress.bin` 再走后面流程。

不管走没走压缩，最后会把 secure 那份拷到 `tmp_app.bin`、NS 那份拷到 `tmp_app_ns.bin`，作为下一步 ota_prepend_head 的输入。NS（non-secure）是 TrustZone 配置专用的并行管线，本工程 `CONFIG_TRUSTZONE=n` 不用，但拷贝照样发生，生成的 `ota_all_ns.bin` 同时存在，烧录时用 `ota_all.bin`。

## 3) `ameba_axf2bin_ota_prepend_head`：套 OTA 头出 `ota_all.bin`

`postbuild.cmake:71-77`：

```cmake
if(CONFIG_UPGRADE_BOOTLOADER)
    ameba_axf2bin_ota_prepend_head(.../ota_all.bin     .../tmp_app.bin    .../km4_boot_all.bin)
    ameba_axf2bin_ota_prepend_head(.../ota_all_ns.bin  .../tmp_app_ns.bin .../km4_boot_all_ns.bin)
else()
    ameba_axf2bin_ota_prepend_head(.../ota_all.bin     .../tmp_app.bin)
    ameba_axf2bin_ota_prepend_head(.../ota_all_ns.bin  .../tmp_app_ns.bin)
endif()
```

本工程走 `else` 分支：`ota_all.bin` 里只有 app（不含 boot）。如果以后要 OTA 升级 bootloader（少见，因为 boot 区在物理上往往不和 app 一起切换），需要把 `CONFIG_UPGRADE_BOOTLOADER` 打开，这一行就会把 `km4_boot_all.bin` 一起塞进 OTA 包。

工具是 `axf2bin.py ota_prepend_header` → `image_process/op_ota_prepend_header.py`。

### OTA 头格式（24 + 8 字节）

`op_ota_prepend_header.py` 干的事：

1. 算 `ota_num = len(input_files)`，本工程 = 1。
2. 算总头长 `offset = 32 + 24*(ota_num - 1)`，本工程 = 32。
3. 用 `parse_image_type()` 按文件名给每个输入分类（IMAGE1 / IMAGE2 / APP_ALL / DSP），其中 `APP_ALL` 的 img_id = 1、`IMAGE1`（即 boot）的 img_id = 0、IMAGE2 的特殊 img_id `0xFF`（跳过单独头）。
4. 顺序固定为 `IMAGE1 → IMAGE2/APP_ALL → DSP`，命令行参数顺序不影响。
5. 输出文件 = `<8 字节 file_header> + <每个文件一条 24 字节 sub_header（IMAGE2 不写）> + <各文件原内容拼接>`。

文件头（8 B）：
```
+0x00  uint32  version       = 0xFFFFFFFF
+0x04  uint32  ota_num       = 输入文件个数
```

每个 sub_header（24 B，big-endian sign，其它 little-endian）：
```
+0x00  uint32  ota_sign      = 0x4F544131 ('OTA1', big-endian)
+0x04  uint32  header_length = 24
+0x08  uint32  check_sum     = SUM(每个字节) of the file，process() 第二趟覆盖填进去
+0x0c  uint32  image_size    = 该文件原始大小
+0x10  uint32  offset        = 该文件在 ota_all.bin 内的偏移
+0x14  uint32  image_id      = 0 (boot) / 1 (app) / 2 (dsp)
```

实测本工程 ota_all.bin 前 32 B：

```
00000000: ffff ffff 0100 0000 4f54 4131 1800 0000  ........OTA1....
00000010: add2 e206 e0e3 1000 2000 0000 0100 0000  ........ .......
00000020: 3831 3935 3837 3131 ...                  81958711...
```

对应解析：

| 偏移 | 字段 | 值 | 含义 |
|------|------|-----|------|
| `0x00` | version | `0xFFFFFFFF` | OTA 版本标记 |
| `0x04` | ota_num | `1` | 只有 app |
| `0x08` | sign | `'OTA1'` (BE) | sub_header 起点 |
| `0x0c` | header_length | `24` | sub_header 本身长度 |
| `0x10` | check_sum | `0x06e2d2ad` | km0_km4_app.bin 字节累加和 |
| `0x14` | image_size | `0x0010e3e0` = 1,106,912 | == km0_km4_app.bin 大小 |
| `0x18` | offset | `0x20` = 32 | == 文件头 + sub_header 总长 |
| `0x1c` | image_id | `1` | APP_ALL |

`0x20` 之后开始就是 `km0_km4_app.bin` 原样内容（首 4 B 的 `'81958711'` 是 km4 image header 里的 firmware version magic）。

## 4) 收尾

postbuild.cmake 最后两步（`platform/ameba-rtos/component/soc/amebadplus/project/postbuild.cmake:78-84`）：

```cmake
# 删中间产物
remove tmp_app.bin / tmp_app_ns.bin

# 拷到上层 FINAL_IMAGE_DIR （= project/ameba/build_RTL8711Dx/）
copy ota_all.bin     → ${FINAL_IMAGE_DIR}
copy km0_km4_app.bin → ${FINAL_IMAGE_DIR}
```

这样 `project/ameba/build_RTL8711Dx/ota_all.bin` 就齐了。

# 入口

```
make amebadplus
  └─ source env.sh
  └─ python $AMEBA_SDK/ameba.py build RTL8711Dx
       ├─ cmake -S component/soc/amebadplus/project -B build_RTL8711Dx/build -G Ninja ...
       ├─ cmake --build build_RTL8711Dx/build --parallel
       │    ├─ 各核 image1/image2 链接 + per-image postbuild
       │    │    → km0_image2_all.bin / km4_image2_all.bin / km4_boot_all.bin
       │    └─ firmware_package 目标
       │         └─ cmake -P component/soc/amebadplus/project/postbuild.cmake
       │              ├─ ameba_axf2bin_fw_pack    → km0_km4_app.bin
       │              ├─ ameba_axf2bin_ota_prepend_head → ota_all.bin
       │              └─ copy ota_all.bin → FINAL_IMAGE_DIR
       └─ ...
```

# 镜像版本号字段 `img_ver_major / img_ver_minor`

一个容易踩坑的点：`platform/ameba-rtos/component/soc/amebadplus/project/manifest.json5` 里给 `image1/image2/cert` 配的 `img_ver_major / img_ver_minor` **不在 OTA wrapper 头里**。上面 `op_ota_prepend_header` 拼出来的那 32 B header 字段就那么几个常量（version=`0xFFFFFFFF`、ota_num、sign、length、checksum、size、offset、img_id），没有版本号位置。

`img_ver_*` 真正落在 **per-image 的 Manifest 头**里。Manifest 头是 SDK 的 image 烧录格式的一部分，被 prepend 到原始 image2 数据之前，整体构成 `kmX_image2_all.bin`，所以从 ota_all.bin 角度看 Manifest 是在 OTA wrapper 之后的负载内容里。

## Manifest 结构

`tools/scripts/image_process/manifest_manager.py:55-70` 定义：

```python
class Manifest_TypeDef(Structure):
    _fields_ = [
        ('Pattern',     c_uint32 * 2),   # +0x00, 8 B   → "81958711"
        ('Rsvd1',       c_uint8  * 7),   # +0x08
        ('RsipCfg',     c_uint8),        # +0x0F
        ('Ver',         c_uint8),        # +0x10
        ('ImgID',       c_uint8),        # +0x11
        ('AuthAlg',     c_uint8),        # +0x12
        ('HashAlg',     c_uint8),        # +0x13
        ('MajorImgVer', c_uint16),       # +0x14  ←── img_ver_major
        ('MinorImgVer', c_uint16),       # +0x16  ←── img_ver_minor
        ('ImgSize',     c_uint32),       # +0x18
        ...
    ]
```

赋值在 `manifest_manager.py:674-675` 的 `create_manifest()`：

```python
basic_manifest_part.MajorImgVer = image_config.img_ver_major
basic_manifest_part.MinorImgVer = image_config.img_ver_minor
```

每张需要 manifest 的 image（image1=boot、KM0 image2、KM4 image2）build 完都会跑 `create_manifest()`，把 manifest.json5 里对应那段 `imageN.img_ver_*` 填进自己 manifest 头的 `+0x14 / +0x16`。

image2 还另有一份 `Certificate_TypeDef`（同 py 文件 `:91-104`），结构里同样有 `MajorKeyVer/MinorKeyVer` 在 `+0x14/+0x16`，由 `create_cert():446-447` 从 `image2.img_ver_*` 填入。sboot 校验时 cert 的 KeyVer 要 >= image manifest 的 ImgVer 才放行升级，所以两边版本号在 manifest.json5 里都来自同一对字段。

## 在 ota_all.bin 里的位置

`fw_pack` 把 `km0_image2_all.bin` 摆在最前（由 `ameba_firmware_package(p_MCU_PROJECTS km0 km4)` 的顺序决定），所以从 ota_all.bin 看：

```
ota_all.bin offset
0x00  ┌──────────────────────────────┐
      │ OTA wrapper file_header (8B) │  version=0xFFFFFFFF, ota_num=1
0x08  │ OTA sub_header (24B, APP_ALL)│  sign/length/checksum/size/offset/img_id
0x20  ├──────────────────────────────┤  ← km0_km4_app.bin 起点
      │ KM0 image2 Manifest          │
      │   +0x00 Pattern "81958711"   │
      │   +0x14 MajorImgVer  ←── 这里│
      │   +0x16 MinorImgVer  ←── 这里│
      │   ...                        │
      ├──────────────────────────────┤
      │ KM0 image2 payload           │
      ├──────────────────────────────┤
      │ KM4 image2 Manifest          │  ← 同样在 +0x14/+0x16 装 img_ver
      ├──────────────────────────────┤
      │ KM4 image2 payload           │
      └──────────────────────────────┘
```

实测本工程 ota_all.bin（`img_ver_major: 1, img_ver_minor: 1`）：

```
00000020: 3831 3935 3837 3131 ffff ffff ffff ffff  ← Pattern + Rsvd1+RsipCfg
00000030: 0101 ffff 0100 0100 d400 0000 0100 0000
          ^^^^ ^^^^ ^^^^ ^^^^
          Ver  ImgID│    │
          =01  =01  │    └── MinorImgVer = 0x0001
                   AuthAlg/HashAlg=0xFF (sboot_enable=false)
                   └── MajorImgVer = 0x0001
```

偏移 `0x34` 上 `01 00 01 00` 正好是 `img_ver_major=1, img_ver_minor=1` 两个 uint16 LE，与 manifest.json5 对得上。改成 `img_ver_major: 2, img_ver_minor: 5` 这四字节会变成 `02 00 05 00`。

## 取值约束

`manifest_manager.py:341-350` 的 `validate_config`：

| `img_id` | 允许范围 |
|----------|----------|
| `IMGID_BOOT (0)` | `[0, 0x7FFF]`（最高位被 boot 留作其它标记） |
| 其它 | `[0, 0xFFFF]` |

# 影响 `ota_all.bin` 内容的 Kconfig

| 开关 | 默认 | 影响 |
|------|------|------|
| `CONFIG_TRUSTZONE` | n | 开启时 `fw_pack` 多吃一份 image3（secure image）一起 pack 进 `km0_km4_app.bin` |
| `CONFIG_UPGRADE_BOOTLOADER` | n | 开启时 `ota_prepend_head` 多吃一份 `km4_boot_all.bin`，OTA 头从 32 B 变成 56 B（多一条 sub_header），并允许通过 OTA 升级 boot 区 |
| `CONFIG_COMPRESS_OTA_IMG` | n | 开启时先用 `ameba_axf2bin_compress` 压一遍 `km0_km4_app.bin` 再喂给 `ota_prepend_head`，OTA 包体积变小但运行时要先解压 |
| `CONFIG_FATFS_WITHIN_APP_IMG` | n | 开启时 4 KB 对齐 `km0_km4_app.bin` 后把 `fatfs.bin` cat 到末尾，再走后面流程，等于把 FATFS 镜像一起 OTA |
| `CONFIG_MP_INCLUDED` | n | 开启时 `${app_name}` 从 `km0_km4_app.bin` 变 `km0_km4_app_mp.bin`，OTA 包基于 MP 镜像 |

# Green2 的差异

`platform/ameba-rtos/component/soc/amebagreen2/project/postbuild.cmake` 流程一模一样，区别只是核名：

- `km4tz` 取代 amebadplus 里 `km4`（image1 = boot）
- `km4ns` 取代 amebadplus 里 `km0`（image2 = app non-secure）
- 输出文件名一致，照样是 `ota_all.bin`

# AmebaD（旧 SDK）不走这套

AmebaD（`platform/ameba-rtos-d`、RTL8721D）是另一个 Make-based SDK，OTA 镜像 `OTA_All.bin` 由 SDK 自己的 makefile 工具链产，不和这里的 `ameba_axf2bin_*` 共享代码。本文档不覆盖。
